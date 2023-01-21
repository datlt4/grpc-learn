/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// For both
#include <grpc/support/log.h>
#include <grpcpp/grpcpp.h>

#include "common/utils.h"
#include "helloworld.grpc.pb.h"

#define CLIENT_V1

// For Server
class ServerImpl final
{
  public:
    ~ServerImpl()
    {
        server_->Shutdown();
        // Always shutdown the completion queue after the server.
        cq_->Shutdown();
    }

    // There is no shutdown handling in this code.
    void Run(std::string &server_address)
    {
        grpc::ServerBuilder builder;
        // Listen on the given address without any authentication mechanism.
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        // Register "service_" as the instance through which we'll communicate with
        // clients. In this case it corresponds to an *asynchronous* service.
        builder.RegisterService(&service_);
        // Get hold of the completion queue used for the asynchronous communication
        // with the gRPC runtime.
        cq_ = builder.AddCompletionQueue();
        // Finally assemble the server.
        server_ = builder.BuildAndStart();
        std::cout << "Server listening on " << server_address << std::endl;

        // Proceed to the server's main loop.
        HandleRpcs();
    }

  private:
    // Class encompasing the state and logic needed to serve a request.
    class CallData
    {
      public:
        ~CallData()
        {
        }
        // Take in the "service" instance (in this case representing an asynchronous
        // server) and the completion queue "cq" used for asynchronous communication
        // with the gRPC runtime.
        CallData(helloworld::Greeter::AsyncService *service, grpc::ServerCompletionQueue *cq)
            : service_(service), cq_(cq), responder_(&ctx_), status_(CREATE)
        {
            // Invoke the serving logic right away.
            Proceed();
        }

        void Proceed()
        {
            if (status_ == CREATE)
            {
                // Make this instance progress to the PROCESS state.
                status_ = PROCESS;

                // As part of the initial CREATE state, we *request* that the system
                // start processing SayHello requests. In this request, "this" acts are
                // the tag uniquely identifying the request (so that different CallData
                // instances can serve different requests concurrently), in this case
                // the memory address of this CallData instance.
                service_->RequestSayHello(&ctx_, &request_, &responder_, cq_, cq_, this);
            }
            else if (status_ == PROCESS)
            {
                // Spawn a new CallData instance to serve new clients while we process
                // the one for this CallData. The instance will deallocate itself as
                // part of its FINISH state.
                new CallData(service_, cq_);

                // The actual processing.
                std::cout << "--" << std::endl;
                std::string prefix("Hello ");
                std::this_thread::sleep_for(std::chrono::milliseconds(2806));
                reply_.set_message(prefix + request_.name());

                // And we are done! Let the gRPC runtime know we've finished, using the
                // memory address of this instance as the uniquely identifying tag for
                // the event.
                status_ = FINISH;
                responder_.Finish(reply_, grpc::Status::OK, this);
            }
            else
            {
                GPR_ASSERT(status_ == FINISH);
                // Once in the FINISH state, deallocate ourselves (CallData).
                delete this;
            }
        }

      private:
        // The means of communication with the gRPC runtime for an asynchronous
        // server.
        helloworld::Greeter::AsyncService *service_;
        // The producer-consumer queue where for asynchronous server notifications.
        grpc::ServerCompletionQueue *cq_;
        // Context for the rpc, allowing to tweak aspects of it such as the use
        // of compression, authentication, as well as to send metadata back to the
        // client.
        grpc::ServerContext ctx_;

        // What we get from the client.
        helloworld::HelloRequest request_;
        // What we send back to the client.
        helloworld::HelloReply reply_;

        // The means to get back to the client.
        grpc::ServerAsyncResponseWriter<helloworld::HelloReply> responder_;

        // Let's implement a tiny state machine with the following states.
        enum CallStatus
        {
            CREATE,
            PROCESS,
            FINISH
        };
        CallStatus status_; // The current serving state.
    };

    // This can be run in multiple threads if needed.
    void HandleRpcs()
    {
        // Spawn a new CallData instance to serve new clients.
        new CallData(&service_, cq_.get());
        void *tag; // uniquely identifies a request.
        bool ok;
        while (true)
        {
            // Block waiting to read the next event from the completion queue. The
            // event is uniquely identified by its tag, which in this case is the
            // memory address of a CallData instance.
            // The return value of Next should always be checked. This return value
            // tells us whether there is any kind of event or cq_ is shutting down.
            GPR_ASSERT(cq_->Next(&tag, &ok));
            GPR_ASSERT(ok);
            static_cast<CallData *>(tag)->Proceed();
        }
    }

    std::unique_ptr<grpc::ServerCompletionQueue> cq_;
    helloworld::Greeter::AsyncService service_;
    std::unique_ptr<grpc::Server> server_;
};

// For Client
class GreeterClient
{
  public:
    explicit GreeterClient(std::shared_ptr<grpc::Channel> channel) : stub_(helloworld::Greeter::NewStub(channel))
    {
    }

    // Assembles the client's payload, sends it and presents the response back
    // from the server.
    std::string SayHello(const std::string &user)
    {
        // Data we are sending to the server.
        helloworld::HelloRequest request;
        request.set_name(user);

        // Container for the data we expect from the server.
        helloworld::HelloReply reply;

        // Context for the client. It could be used to convey extra information to
        // the server and/or tweak certain RPC behaviors.
        grpc::ClientContext context;

        // The producer-consumer queue we use to communicate asynchronously with the
        // gRPC runtime.
        grpc::CompletionQueue cq;

        // Storage for the status of the RPC upon completion.
        grpc::Status status;

        std::unique_ptr<grpc::ClientAsyncResponseReader<helloworld::HelloReply>> rpc(
            stub_->AsyncSayHello(&context, request, &cq));

        // Request that, upon completion of the RPC, "reply" be updated with the
        // server's response; "status" with the indication of whether the operation
        // was successful. Tag the request with the integer 1.
        rpc->Finish(&reply, &status, (void *)1);
        void *got_tag;
        bool ok = false;
        // Block until the next result is available in the completion queue "cq".
        // The return value of Next should always be checked. This return value
        // tells us whether there is any kind of event or the cq_ is shutting down.
        GPR_ASSERT(cq.Next(&got_tag, &ok));

        // Verify that the result from "cq" corresponds, by its tag, our previous
        // request.
        GPR_ASSERT(got_tag == (void *)1);
        // ... and that the request was completed successfully. Note that "ok"
        // corresponds solely to the request for updates introduced by Finish().
        GPR_ASSERT(ok);

        // Act upon the status of the actual RPC.
        if (status.ok())
        {
            return reply.message();
        }
        else
        {
            return "RPC failed";
        }
    }

  private:
    // Out of the passed in Channel comes the stub, stored here, our view of the
    // server's exposed services.
    std::unique_ptr<helloworld::Greeter::Stub> stub_;
};

class GreeterClient2
{
  public:
    explicit GreeterClient2(std::shared_ptr<grpc::Channel> channel) : stub_(helloworld::Greeter::NewStub(channel))
    {
    }

    // Assembles the client's payload and sends it to the server.
    void SayHello(const std::string &user)
    {
        // Data we are sending to the server.
        helloworld::HelloRequest request;
        request.set_name(user);

        // Call object to store rpc data
        AsyncClientCall *call = new AsyncClientCall;

        // stub_->PrepareAsyncSayHello() creates an RPC object, returning
        // an instance to store in "call" but does not actually start the RPC
        // Because we are using the asynchronous API, we need to hold on to
        // the "call" instance in order to get updates on the ongoing RPC.
        call->response_reader = stub_->PrepareAsyncSayHello(&call->context, request, &cq_);

        // StartCall initiates the RPC call
        call->response_reader->StartCall();

        // Request that, upon completion of the RPC, "reply" be updated with the
        // server's response; "status" with the indication of whether the operation
        // was successful. Tag the request with the memory address of the call
        // object.
        call->response_reader->Finish(&call->reply, &call->status, (void *)call);
    }

    // Loop while listening for completed responses.
    // Prints out the response from the server.
    void AsyncCompleteRpc()
    {
        void *got_tag;
        bool ok = false;

        // Block until the next result is available in the completion queue "cq".
        while (cq_.Next(&got_tag, &ok))
        {
            // The tag in this example is the memory location of the call object
            AsyncClientCall *call = static_cast<AsyncClientCall *>(got_tag);

            // Verify that the request was completed successfully. Note that "ok"
            // corresponds solely to the request for updates introduced by Finish().
            GPR_ASSERT(ok);

            if (call->status.ok())
                std::cout << "Greeter received: " << call->reply.message() << std::endl;
            else
                std::cout << "RPC failed" << std::endl;

            // Once we're complete, deallocate the call object.
            delete call;
        }
    }

  private:
    // struct for keeping state and data information
    struct AsyncClientCall
    {
        // Container for the data we expect from the server.
        helloworld::HelloReply reply;

        // Context for the client. It could be used to convey extra information to
        // the server and/or tweak certain RPC behaviors.
        grpc::ClientContext context;

        // Storage for the status of the RPC upon completion.
        grpc::Status status;

        std::unique_ptr<grpc::ClientAsyncResponseReader<helloworld::HelloReply>> response_reader;
    };

    // Out of the passed in Channel comes the stub, stored here, our view of the
    // server's exposed services.
    std::unique_ptr<helloworld::Greeter::Stub> stub_;

    // The producer-consumer queue we use to communicate asynchronously with the
    // gRPC runtime.
    grpc::CompletionQueue cq_;
};

// For both
int main(int argc, char **argv)
{
    std::string server_address{"0.0.0.0:50051"};
    Mode mode{Mode::CLIENT};
    ParseCLIState cliState = ParseCommandLine(argc, argv, server_address, mode);
    if (cliState == ParseCLIState::SUCCESS)
    {
#ifdef CLIENT_V1
        if (mode == Mode::CLIENT)
        {
            // Instantiate the client. It requires a channel, out of which the actual RPCs
            // are created. This channel models a connection to an endpoint (in this case,
            // localhost at port 50051). We indicate that the channel isn't authenticated
            // (use of InsecureChannelCredentials()).
            GreeterClient greeter(grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));
            std::string user("world");
            std::string reply = greeter.SayHello(user); // The actual RPC call!
            std::cout << "Greeter received: " << reply << std::endl;
        }
#endif // CLIENT_V1
#ifdef CLIENT_V2
        if (mode == Mode::CLIENT)
        {
            // Instantiate the client. It requires a channel, out of which the actual RPCs
            // are created. This channel models a connection to an endpoint (in this case,
            // localhost at port 50051). We indicate that the channel isn't authenticated
            // (use of InsecureChannelCredentials()).
            GreeterClient2 greeter2(grpc::CreateChannel(server_address, grpc::InsecureChannelCredentials()));

            // Spawn reader thread that loops indefinitely
            std::thread thread_ = std::thread(&GreeterClient2::AsyncCompleteRpc, &greeter2);

            for (int i = 0; i < 100; i++)
            {
                std::string user("world " + std::to_string(i));
                greeter2.SayHello(user); // The actual RPC call!
            }

            std::cout << "Press control-c to quit" << std::endl << std::endl;
            thread_.join(); // blocks forever
        }
#endif       // CLIENT_V2
        else // SERVER
        {
            ServerImpl server;
            server.Run(server_address);
        }

        return 0;
    }
    else if (cliState == ParseCLIState::SHOW_HELP)
        return 0;
    else
        return 1;
}

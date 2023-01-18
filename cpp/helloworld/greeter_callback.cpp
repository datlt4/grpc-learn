/*
 *
 * Copyright 2021 gRPC authors.
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

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

// For Server
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/grpcpp.h>
#include <grpcpp/health_check_service_interface.h>
// For Client
#include <grpcpp/grpcpp.h>
// For both
#include "helloworld.grpc.pb.h"
#include "utils.h"

// For Server
// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public helloworld::Greeter::CallbackService
{
  private:
    int i = 0;
    grpc::ServerUnaryReactor *SayHello(grpc::CallbackServerContext *context, const helloworld::HelloRequest *request,
                                       helloworld::HelloReply *reply) override
    {
        std::cout << "--" << std::endl;
        std::string prefix("Hello ");
        reply->set_message(prefix + request->name());
        reply->set_order(++i);
        grpc::ServerUnaryReactor *reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status::OK);
        return reactor;
    }
};

void RunServer(std::string &server_address)
{
    GreeterServiceImpl service;

    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    grpc::ServerBuilder builder;
    // Listen on the given address without any authentication mechanism.
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    builder.RegisterService(&service);
    // Finally assemble the server.
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
}

// For Client
class GreeterClient
{
  public:
    GreeterClient(std::shared_ptr<grpc::Channel> channel) : stub_(helloworld::Greeter::NewStub(channel))
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

        // The actual RPC.
        std::mutex mu;
        std::condition_variable cv;
        bool done = false;
        grpc::Status status;
        stub_->async()->SayHello(&context, &request, &reply, [&mu, &cv, &done, &status](grpc::Status s) {
            status = std::move(s);
            std::lock_guard<std::mutex> lock(mu);
            done = true;
            cv.notify_one();
        });

        std::unique_lock<std::mutex> lock(mu);
        while (!done)
        {
            cv.wait(lock);
        }

        // Act upon its status.
        if (status.ok())
        {
            return reply.message();
        }
        else
        {
            std::cout << status.error_code() << ": " << status.error_message() << std::endl;
            return "RPC failed";
        }
    }

  private:
    std::unique_ptr<helloworld::Greeter::Stub> stub_;
};

// For both
int main(int argc, char **argv)
{
    std::string server_address{"0.0.0.0:50051"};
    RunServer(server_address);

    return 0;
}

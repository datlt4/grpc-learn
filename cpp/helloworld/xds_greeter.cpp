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

// For both
#include <iostream>
#include <memory>
#include <string>

// For Server
#include "absl/strings/str_cat.h"
#include <grpcpp/ext/admin_services.h>
#include <grpcpp/ext/proto_server_reflection_plugin.h>
#include <grpcpp/health_check_service_interface.h>
#include <grpcpp/xds_server_builder.h>
// For Client

// For both
#include "common/utils.h"
#include "helloworld.grpc.pb.h"
#include <grpcpp/grpcpp.h>

// For Server
// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public Greeter::Service
{
    Status SayHello(ServerContext *context, const HelloRequest *request, HelloReply *reply) override
    {
        std::string prefix("Hello ");
        reply->set_message(prefix + request->name());
        return Status::OK;
    }
};

void RunServer(int port = 50051, int maintenance_port = 50052, bool FLAGS_secure = true, )
{
    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    grpc::XdsServerBuilder xds_builder;
    ServerBuilder builder;
    std::unique_ptr<Server> xds_enabled_server;
    std::unique_ptr<Server> server;
    GreeterServiceImpl service;
    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    xds_builder.RegisterService(&service);
    if (FLAGS_secure)
    {
        // Listen on the given address with XdsServerCredentials and a fallback of
        // InsecureServerCredentials
        std::string server_address = ;
        xds_builder.AddListeningPort(std::string("0.0.0.0") + std::to_string(port),
                                     grpc::XdsServerCredentials(grpc::InsecureServerCredentials()));
        xds_enabled_server = xds_builder.BuildAndStart();
        gpr_log(GPR_INFO, "Server starting on 0.0.0.0:%d", port);
        grpc::AddAdminServices(&builder);
        // For the maintenance server, do not use any authentication mechanism.
        builder.AddListeningPort(std::string("0.0.0.0") + std::to_string(maintenance_port),
                                 grpc::InsecureServerCredentials());
        server = builder.BuildAndStart();
        gpr_log(GPR_INFO, "Maintenance server listening on 0.0.0.0:%d", maintenance_port);
    }
    else
    {
        grpc::AddAdminServices(&xds_builder);
        // Listen on the given address without any authentication mechanism.
        builder.AddListeningPort(std::string("0.0.0.0") + std::to_string(port), grpc::InsecureServerCredentials());
        server = xds_builder.BuildAndStart();
        gpr_log(GPR_INFO, "Server listening on 0.0.0.0:%d", port);
    }

    // Wait for the server to shutdown. Note that some other thread must be
    // responsible for shutting down the server for this call to ever return.
    server->Wait();
}

// For Client
class GreeterClient
{
  public:
    GreeterClient(std::shared_ptr<Channel> channel) : stub_(Greeter::NewStub(channel))
    {
    }

    // Assembles the client's payload, sends it and presents the response back
    // from the server.
    std::string SayHello(const std::string &user)
    {
        // Data we are sending to the server.
        HelloRequest request;
        request.set_name(user);

        // Container for the data we expect from the server.
        HelloReply reply;

        // Context for the client. It could be used to convey extra information to
        // the server and/or tweak certain RPC behaviors.
        ClientContext context;

        // The actual RPC.
        Status status = stub_->SayHello(&context, request, &reply);

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
    std::unique_ptr<Greeter::Stub> stub_;
};

// For both
int main(int argc, char **argv)
{
    std::string server_address{"0.0.0.0:50051"};
    Mode mode{Mode::CLIENT};
    ParseCLIState cliState = ParseCommandLine(argc, argv, server_address, mode);
    if (cliState == ParseCLIState::SUCCESS)
    {
        if (mode == Mode::SERVER)
        {
            RunServer(50051, 50052, true);
        }
        else
        {
            assert(mode == Mode::CLIENT);
            GreeterClient greeter(grpc::CreateChannel(absl::GetFlag(FLAGS_target),
                                                      absl::GetFlag(FLAGS_secure)
                                                          ? grpc::XdsCredentials(grpc::InsecureChannelCredentials())
                                                          : grpc::InsecureChannelCredentials()));
            std::string user("world");
            std::string reply = greeter.SayHello(user);
            std::cout << "Greeter received: " << reply << std::endl;
            return 0;
        }
        else if (cliState == ParseCLIState::SHOW_HELP) return 0;
        else return 1;
    }
}

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
#include <grpcpp/security/alts_util.h>
#include <grpcpp/xds_server_builder.h>
// For Client

// For both
#include "common/utils.h"
#include "helloworld.grpc.pb.h"
#include <grpcpp/grpcpp.h>

// For Server
// Logic and data behind the server's behavior.
class GreeterServiceImpl final : public helloworld::Greeter::Service
{
    grpc::Status SayHello(grpc::ServerContext *context, const helloworld::HelloRequest *request,
                          helloworld::HelloReply *reply) override
    {
        std::string prefix("Hello ");
        reply->set_message(prefix + request->name());
        return grpc::Status::OK;
    }
};

void RunServer(std::string &server_address, std::string &maintenance_address, bool FLAGS_secure = true)
{
    grpc::EnableDefaultHealthCheckService(true);
    grpc::reflection::InitProtoReflectionServerBuilderPlugin();
    grpc::XdsServerBuilder xds_builder;
    grpc::ServerBuilder builder;
    std::unique_ptr<grpc::Server> xds_enabled_server;
    std::unique_ptr<grpc::Server> server;
    GreeterServiceImpl service;
    // Register "service" as the instance through which we'll communicate with
    // clients. In this case it corresponds to an *synchronous* service.
    xds_builder.RegisterService(&service);
    if (FLAGS_secure)
    {
        // Listen on the given address with XdsServerCredentials and a fallback of
        // InsecureServerCredentials
        xds_builder.AddListeningPort(server_address, grpc::XdsServerCredentials(grpc::InsecureServerCredentials()));
        xds_enabled_server = xds_builder.BuildAndStart();
        gpr_log(GPR_INFO, (std::string("Server starting on ") + server_address).c_str());
        grpc::AddAdminServices(&builder);
        // For the maintenance server, do not use any authentication mechanism.
        builder.AddListeningPort(maintenance_address, grpc::InsecureServerCredentials());
        server = builder.BuildAndStart();
        gpr_log(GPR_INFO, (std::string("Maintenance server listening on ") + maintenance_address).c_str());
    }
    else
    {
        grpc::AddAdminServices(&xds_builder);
        // Listen on the given address without any authentication mechanism.
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        server = xds_builder.BuildAndStart();
        gpr_log(GPR_INFO, (std::string("Server listening on ") + server_address).c_str());
    }

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
        grpc::Status status = stub_->SayHello(&context, request, &reply);

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
    CliParams cli_params;
    ParseCLIState cliState = ParseCommandLine(argc, argv, &cli_params);
    if (cliState == ParseCLIState::SUCCESS)
    {
        if (cli_params.mode == Mode::SERVER)
        {
            RunServer(cli_params.server_address, cli_params.maintenance_address, cli_params.secure);
        }
        else
        {
            assert(cli_params.mode == Mode::CLIENT);
            GreeterClient greeter(grpc::CreateChannel(
                cli_params.server_address, cli_params.secure ? grpc::XdsCredentials(grpc::InsecureChannelCredentials())
                                                             : grpc::InsecureChannelCredentials()));
            std::string user("world");
            std::string reply = greeter.SayHello(user);
            std::cout << "Greeter received: " << reply << std::endl;
            return 0;
        }
    }
    else if (cliState == ParseCLIState::SHOW_HELP)
        return 0;
    else
        return 1;
}

// ./xds_greeter -c --server_address "xds:///helloworld:50051" --secure
// ./xds_greeter -s --server_address "xds:///helloworld:50051" --secure

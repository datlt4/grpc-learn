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

// For both
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <thread>

// For Server
#include <grpc/grpc.h>
#include <grpcpp/security/server_credentials.h>
#include <grpcpp/server.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>

// For Client
#include <grpc/grpc.h>
#include <grpcpp/channel.h>
#include <grpcpp/client_context.h>
#include <grpcpp/create_channel.h>
#include <grpcpp/security/credentials.h>

// For both
#include "common/utils.h"
#include "helper.h"
#include "route_guide.grpc.pb.h"

// For Server
float ConvertToRadians(float num)
{
    return num * 3.1415926 / 180;
}

// The formula is based on http://mathforum.org/library/drmath/view/51879.html
float GetDistance(const routeguide::Point &start, const routeguide::Point &end)
{
    const float kCoordFactor = 10000000.0;
    float lat_1 = start.latitude() / kCoordFactor;
    float lat_2 = end.latitude() / kCoordFactor;
    float lon_1 = start.longitude() / kCoordFactor;
    float lon_2 = end.longitude() / kCoordFactor;
    float lat_rad_1 = ConvertToRadians(lat_1);
    float lat_rad_2 = ConvertToRadians(lat_2);
    float delta_lat_rad = ConvertToRadians(lat_2 - lat_1);
    float delta_lon_rad = ConvertToRadians(lon_2 - lon_1);

    float a = pow(sin(delta_lat_rad / 2), 2) + cos(lat_rad_1) * cos(lat_rad_2) * pow(sin(delta_lon_rad / 2), 2);
    float c = 2 * atan2(sqrt(a), sqrt(1 - a));
    int R = 6371000; // metres

    return R * c;
}

std::string GetFeatureName(const routeguide::Point &point, const std::vector<routeguide::Feature> &feature_list)
{
    for (const routeguide::Feature &f : feature_list)
    {
        if (f.location().latitude() == point.latitude() && f.location().longitude() == point.longitude())
        {
            return f.name();
        }
    }
    return "";
}

class RouteGuideImpl final : public routeguide::RouteGuide::Service
{
  public:
    explicit RouteGuideImpl(const std::string &db)
    {
        routeguide::ParseDb(db, &feature_list_);
    }

    grpc::Status GetFeature(grpc::ServerContext *context, const routeguide::Point *point,
                            routeguide::Feature *feature) override
    {
        feature->set_name(GetFeatureName(*point, feature_list_));
        feature->mutable_location()->CopyFrom(*point);
        return grpc::Status::OK;
    }

    grpc::Status ListFeatures(grpc::ServerContext *context, const routeguide::Rectangle *rectangle,
                              grpc::ServerWriter<routeguide::Feature> *writer) override
    {
        auto lo = rectangle->lo();
        auto hi = rectangle->hi();
        long left = (std::min)(lo.longitude(), hi.longitude());
        long right = (std::max)(lo.longitude(), hi.longitude());
        long top = (std::max)(lo.latitude(), hi.latitude());
        long bottom = (std::min)(lo.latitude(), hi.latitude());
        // for (const Feature &f : feature_list_)
        for (const routeguide::Feature &f : feature_list_)
        {
            if (f.location().longitude() >= left && f.location().longitude() <= right &&
                f.location().latitude() >= bottom && f.location().latitude() <= top)
            {
                writer->Write(f);
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status RecordRoute(grpc::ServerContext *context, grpc::ServerReader<routeguide::Point> *reader,
                             routeguide::RouteSummary *summary) override
    {
        routeguide::Point point;
        int point_count = 0;
        int feature_count = 0;
        float distance = 0.0;
        routeguide::Point previous;

        std::chrono::system_clock::time_point start_time = std::chrono::system_clock::now();
        while (reader->Read(&point))
        {
            point_count++;
            if (!GetFeatureName(point, feature_list_).empty())
            {
                feature_count++;
            }
            if (point_count != 1)
            {
                distance += GetDistance(previous, point);
            }
            previous = point;
        }
        std::chrono::system_clock::time_point end_time = std::chrono::system_clock::now();
        summary->set_point_count(point_count);
        summary->set_feature_count(feature_count);
        summary->set_distance(static_cast<long>(distance));
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);
        summary->set_elapsed_time(secs.count());

        return grpc::Status::OK;
    }

    grpc::Status RouteChat(grpc::ServerContext *context,
                           grpc::ServerReaderWriter<routeguide::RouteNote, routeguide::RouteNote> *stream) override
    {
        routeguide::RouteNote note;
        while (stream->Read(&note))
        {
            std::unique_lock<std::mutex> lock(mu_);
            for (const routeguide::RouteNote &n : received_notes_)
            {
                if (n.location().latitude() == note.location().latitude() &&
                    n.location().longitude() == note.location().longitude())
                {
                    stream->Write(n);
                }
            }
            received_notes_.push_back(note);
        }

        return grpc::Status::OK;
    }

  private:
    std::vector<routeguide::Feature> feature_list_;
    std::mutex mu_;
    std::vector<routeguide::RouteNote> received_notes_;
};

void RunServer(const std::string &db, std::string &server_address)
{
    RouteGuideImpl service(db);

    grpc::ServerBuilder builder;
    builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
    std::cout << "Server listening on " << server_address << std::endl;
    server->Wait();
}

// For Client
routeguide::Point MakePoint(long latitude, long longitude)
{
    routeguide::Point p;
    p.set_latitude(latitude);
    p.set_longitude(longitude);
    return p;
}

routeguide::Feature MakeFeature(const std::string &name, long latitude, long longitude)
{
    routeguide::Feature f;
    f.set_name(name);
    f.mutable_location()->CopyFrom(MakePoint(latitude, longitude));
    return f;
}

routeguide::RouteNote MakeRouteNote(const std::string &message, long latitude, long longitude)
{
    routeguide::RouteNote n;
    n.set_message(message);
    n.mutable_location()->CopyFrom(MakePoint(latitude, longitude));
    return n;
}

class RouteGuideClient
{
  public:
    RouteGuideClient(std::shared_ptr<grpc::Channel> channel, const std::string &db)
        : stub_(routeguide::RouteGuide::NewStub(channel))
    {
        routeguide::ParseDb(db, &feature_list_);
    }

    void GetFeature()
    {
        routeguide::Point point;
        routeguide::Feature feature;
        point = MakePoint(409146138, -746188906);
        GetOneFeature(point, &feature);
        point = MakePoint(0, 0);
        GetOneFeature(point, &feature);
    }

    void ListFeatures()
    {
        routeguide::Rectangle rect;
        routeguide::Feature feature;
        grpc::ClientContext context;

        rect.mutable_lo()->set_latitude(400000000);
        rect.mutable_lo()->set_longitude(-750000000);
        rect.mutable_hi()->set_latitude(420000000);
        rect.mutable_hi()->set_longitude(-730000000);
        std::cout << "Looking for features between 40, -75 and 42, -73" << std::endl;

        std::unique_ptr<grpc::ClientReader<routeguide::Feature>> reader(stub_->ListFeatures(&context, rect));
        while (reader->Read(&feature))
        {
            std::cout << "Found feature called " << feature.name() << " at "
                      << feature.location().latitude() / kCoordFactor_ << ", "
                      << feature.location().longitude() / kCoordFactor_ << std::endl;
        }
        grpc::Status status = reader->Finish();
        if (status.ok())
        {
            std::cout << "ListFeatures rpc succeeded." << std::endl;
        }
        else
        {
            std::cout << "ListFeatures rpc failed." << std::endl;
        }
    }

    void RecordRoute()
    {
        routeguide::Point point;
        routeguide::RouteSummary stats;
        grpc::ClientContext context;
        const int kPoints = 10;
        unsigned seed = std::chrono::system_clock::now().time_since_epoch().count();

        std::default_random_engine generator(seed);
        std::uniform_int_distribution<int> feature_distribution(0, feature_list_.size() - 1);
        std::uniform_int_distribution<int> delay_distribution(500, 1500);

        std::unique_ptr<grpc::ClientWriter<routeguide::Point>> writer(stub_->RecordRoute(&context, &stats));
        for (int i = 0; i < kPoints; i++)
        {
            const routeguide::Feature &f = feature_list_[feature_distribution(generator)];
            std::cout << "Visiting point " << f.location().latitude() / kCoordFactor_ << ", "
                      << f.location().longitude() / kCoordFactor_ << std::endl;
            if (!writer->Write(f.location()))
            {
                // Broken stream.
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_distribution(generator)));
        }
        writer->WritesDone();
        grpc::Status status = writer->Finish();
        if (status.ok())
        {
            std::cout << "Finished trip with " << stats.point_count() << " points\n"
                      << "Passed " << stats.feature_count() << " features\n"
                      << "Travelled " << stats.distance() << " meters\n"
                      << "It took " << stats.elapsed_time() << " seconds" << std::endl;
        }
        else
        {
            std::cout << "RecordRoute rpc failed." << std::endl;
        }
    }

    void RouteChat()
    {
        grpc::ClientContext context;

        std::shared_ptr<grpc::ClientReaderWriter<routeguide::RouteNote, routeguide::RouteNote>> stream(
            stub_->RouteChat(&context));

        std::thread writer([stream]() {
            std::vector<routeguide::RouteNote> notes{
                MakeRouteNote("First message", 0, 0), MakeRouteNote("Second message", 0, 1),
                MakeRouteNote("Third message", 1, 0), MakeRouteNote("Fourth message", 0, 0)};
            for (const routeguide::RouteNote &note : notes)
            {
                std::cout << "Sending message " << note.message() << " at " << note.location().latitude() << ", "
                          << note.location().longitude() << std::endl;
                stream->Write(note);
            }
            stream->WritesDone();
        });

        routeguide::RouteNote server_note;
        while (stream->Read(&server_note))
        {
            std::cout << "Got message " << server_note.message() << " at " << server_note.location().latitude() << ", "
                      << server_note.location().longitude() << std::endl;
        }
        writer.join();
        grpc::Status status = stream->Finish();
        if (!status.ok())
        {
            std::cout << "RouteChat rpc failed." << std::endl;
        }
    }

  private:
    bool GetOneFeature(const routeguide::Point &point, routeguide::Feature *feature)
    {
        grpc::ClientContext context;
        grpc::Status status = stub_->GetFeature(&context, point, feature);
        if (!status.ok())
        {
            std::cout << "GetFeature rpc failed." << std::endl;
            return false;
        }
        if (!feature->has_location())
        {
            std::cout << "Server returns incomplete feature." << std::endl;
            return false;
        }
        if (feature->name().empty())
        {
            std::cout << "Found no feature at " << feature->location().latitude() / kCoordFactor_ << ", "
                      << feature->location().longitude() / kCoordFactor_ << std::endl;
        }
        else
        {
            std::cout << "Found feature called " << feature->name() << " at "
                      << feature->location().latitude() / kCoordFactor_ << ", "
                      << feature->location().longitude() / kCoordFactor_ << std::endl;
        }
        return true;
    }

    const float kCoordFactor_ = 10000000.0;
    std::unique_ptr<routeguide::RouteGuide::Stub> stub_;
    std::vector<routeguide::Feature> feature_list_;
};

// For both
int main(int argc, char **argv)
{
    CliParams cli_params;
    ParseCLIState cliState = ParseCommandLine(argc, argv, &cli_params);
    std::string db_content = routeguide::GetDbFileContent(cli_params.database);

    if (cliState == ParseCLIState::SUCCESS)
    {
        if (cli_params.mode == Mode::CLIENT)
        {
            RouteGuideClient route_guide(
                grpc::CreateChannel(cli_params.server_address, grpc::InsecureChannelCredentials()), db_content);

            std::cout << "-------------- GetFeature --------------" << std::endl;
            route_guide.GetFeature();
            std::cout << "-------------- ListFeatures --------------" << std::endl;
            route_guide.ListFeatures();
            std::cout << "-------------- RecordRoute --------------" << std::endl;
            route_guide.RecordRoute();
            std::cout << "-------------- RouteChat --------------" << std::endl;
            route_guide.RouteChat();
        }
        else // SERVER
        {
            RunServer(db_content, cli_params.server_address);
        }
        return 0;
    }
    else if (cliState == ParseCLIState::SHOW_HELP)
        return 0;
    else
        return 1;
}

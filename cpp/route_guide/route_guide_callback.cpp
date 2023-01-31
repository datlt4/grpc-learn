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
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
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
#include <grpcpp/alarm.h>
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

class RouteGuideImpl final : public routeguide::RouteGuide::CallbackService
{
  public:
    explicit RouteGuideImpl(const std::string &db)
    {
        routeguide::ParseDb(db, &feature_list_);
    }

    grpc::ServerUnaryReactor *GetFeature(grpc::CallbackServerContext *context, const routeguide::Point *point,
                                         routeguide::Feature *feature) override
    {
        feature->set_name(GetFeatureName(*point, feature_list_));
        feature->mutable_location()->CopyFrom(*point);
        grpc::ServerUnaryReactor *reactor = context->DefaultReactor();
        reactor->Finish(grpc::Status::OK);
        return reactor;
    }

    grpc::ServerWriteReactor<routeguide::Feature> *ListFeatures(grpc::CallbackServerContext *context,
                                                                const routeguide::Rectangle *rectangle) override
    {
        class Lister : public grpc::ServerWriteReactor<routeguide::Feature>
        {
          public:
            Lister(const routeguide::Rectangle *rectangle, const std::vector<routeguide::Feature> *feature_list)
                : left_((std::min)(rectangle->lo().longitude(), rectangle->hi().longitude())),
                  right_((std::max)(rectangle->lo().longitude(), rectangle->hi().longitude())),
                  top_((std::max)(rectangle->lo().latitude(), rectangle->hi().latitude())),
                  bottom_((std::min)(rectangle->lo().latitude(), rectangle->hi().latitude())),
                  feature_list_(feature_list), next_feature_(feature_list_->begin())
            {
                NextWrite();
            }
            void OnDone() override
            {
                delete this;
            }
            void OnWriteDone(bool /*ok*/) override
            {
                NextWrite();
            }

          private:
            void NextWrite()
            {
                while (next_feature_ != feature_list_->end())
                {
                    const routeguide::Feature &f = *next_feature_;
                    next_feature_++;
                    if (f.location().longitude() >= left_ && f.location().longitude() <= right_ &&
                        f.location().latitude() >= bottom_ && f.location().latitude() <= top_)
                    {
                        StartWrite(&f);
                        return;
                    }
                }
                // Didn't write anything, all is done.
                Finish(grpc::Status::OK);
            }
            const long left_;
            const long right_;
            const long top_;
            const long bottom_;
            const std::vector<routeguide::Feature> *feature_list_;
            std::vector<routeguide::Feature>::const_iterator next_feature_;
        };
        return new Lister(rectangle, &feature_list_);
    }

    grpc::ServerReadReactor<routeguide::Point> *RecordRoute(grpc::CallbackServerContext *context,
                                                            routeguide::RouteSummary *summary) override
    {
        class Recorder : public grpc::ServerReadReactor<routeguide::Point>
        {
          public:
            Recorder(routeguide::RouteSummary *summary, const std::vector<routeguide::Feature> *feature_list)
                : start_time_(std::chrono::system_clock::now()), summary_(summary), feature_list_(feature_list)
            {
                StartRead(&point_);
            }
            void OnDone() override
            {
                delete this;
            }
            void OnReadDone(bool ok) override
            {
                if (ok)
                {
                    point_count_++;
                    if (!GetFeatureName(point_, *feature_list_).empty())
                    {
                        feature_count_++;
                    }
                    if (point_count_ != 1)
                    {
                        distance_ += GetDistance(previous_, point_);
                    }
                    previous_ = point_;
                    StartRead(&point_);
                }
                else
                {
                    summary_->set_point_count(point_count_);
                    summary_->set_feature_count(feature_count_);
                    summary_->set_distance(static_cast<long>(distance_));
                    auto secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() -
                                                                                 start_time_);
                    summary_->set_elapsed_time(secs.count());
                    Finish(grpc::Status::OK);
                }
            }

          private:
            std::chrono::system_clock::time_point start_time_;
            routeguide::RouteSummary *summary_;
            const std::vector<routeguide::Feature> *feature_list_;
            routeguide::Point point_;
            int point_count_ = 0;
            int feature_count_ = 0;
            float distance_ = 0.0;
            routeguide::Point previous_;
        };
        return new Recorder(summary, &feature_list_);
    }

    grpc::ServerBidiReactor<routeguide::RouteNote, routeguide::RouteNote> *RouteChat(
        grpc::CallbackServerContext *context) override
    {
        class Chatter : public grpc::ServerBidiReactor<routeguide::RouteNote, routeguide::RouteNote>
        {
          public:
            Chatter(absl::Mutex *mu, std::vector<routeguide::RouteNote> *received_notes)
                : mu_(mu), received_notes_(received_notes)
            {
                StartRead(&note_);
            }
            void OnDone() override
            {
                delete this;
            }
            void OnReadDone(bool ok) override
            {
                if (ok)
                {
                    // Unlike the other example in this directory that's not using
                    // the reactor pattern, we can't grab a local lock to secure the
                    // access to the notes vector, because the reactor will most likely
                    // make us jump threads, so we'll have to use a different locking
                    // strategy. We'll grab the lock locally to build a copy of the
                    // list of nodes we're going to send, then we'll grab the lock
                    // again to append the received note to the existing vector.
                    mu_->Lock();
                    std::copy_if(received_notes_->begin(), received_notes_->end(), std::back_inserter(to_send_notes_),
                                 [this](const routeguide::RouteNote &note) {
                                     return note.location().latitude() == note_.location().latitude() &&
                                            note.location().longitude() == note_.location().longitude();
                                 });
                    mu_->Unlock();
                    notes_iterator_ = to_send_notes_.begin();
                    NextWrite();
                }
                else
                {
                    Finish(grpc::Status::OK);
                }
            }
            void OnWriteDone(bool /*ok*/) override
            {
                NextWrite();
            }

          private:
            void NextWrite()
            {
                if (notes_iterator_ != to_send_notes_.end())
                {
                    StartWrite(&*notes_iterator_);
                    notes_iterator_++;
                }
                else
                {
                    mu_->Lock();
                    received_notes_->push_back(note_);
                    mu_->Unlock();
                    StartRead(&note_);
                }
            }
            routeguide::RouteNote note_;
            absl::Mutex *mu_;
            std::vector<routeguide::RouteNote> *received_notes_;
            std::vector<routeguide::RouteNote> to_send_notes_;
            std::vector<routeguide::RouteNote>::iterator notes_iterator_;
        };
        return new Chatter(&mu_, &received_notes_);
    }

  private:
    std::vector<routeguide::Feature> feature_list_;
    absl::Mutex mu_;
    std::vector<routeguide::RouteNote> received_notes_ ABSL_GUARDED_BY(mu_);
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

        rect.mutable_lo()->set_latitude(400000000);
        rect.mutable_lo()->set_longitude(-750000000);
        rect.mutable_hi()->set_latitude(420000000);
        rect.mutable_hi()->set_longitude(-730000000);
        std::cout << "Looking for features between 40, -75 and 42, -73" << std::endl;

        class Reader : public grpc::ClientReadReactor<routeguide::Feature>
        {
          public:
            Reader(routeguide::RouteGuide::Stub *stub, float coord_factor, const routeguide::Rectangle &rect)
                : coord_factor_(coord_factor)
            {
                stub->async()->ListFeatures(&context_, &rect, this);
                StartRead(&feature_);
                StartCall();
            }
            void OnReadDone(bool ok) override
            {
                if (ok)
                {
                    if (feature_.name().empty())
                        std::cout << "Found feature at " << feature_.location().latitude() / coord_factor_ << ", "
                                  << feature_.location().longitude() / coord_factor_ << std::endl;
                    else
                        std::cout << "Found feature called " << feature_.name() << " at "
                                  << feature_.location().latitude() / coord_factor_ << ", "
                                  << feature_.location().longitude() / coord_factor_ << std::endl;
                    StartRead(&feature_);
                }
            }
            void OnDone(const grpc::Status &s) override
            {
                std::unique_lock<std::mutex> l(mu_);
                status_ = s;
                done_ = true;
                cv_.notify_one();
            }
            grpc::Status Await()
            {
                std::unique_lock<std::mutex> l(mu_);
                cv_.wait(l, [this] { return done_; });
                return std::move(status_);
            }

          private:
            grpc::ClientContext context_;
            float coord_factor_;
            routeguide::Feature feature_;
            std::mutex mu_;
            std::condition_variable cv_;
            grpc::Status status_;
            bool done_ = false;
        };
        Reader reader(stub_.get(), kCoordFactor_, rect);
        grpc::Status status = reader.Await();
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
        class Recorder : public grpc::ClientWriteReactor<routeguide::Point>
        {
          public:
            Recorder(routeguide::RouteGuide::Stub *stub, float coord_factor,
                     const std::vector<routeguide::Feature> *feature_list)
                : coord_factor_(coord_factor), feature_list_(feature_list),
                  generator_(std::chrono::system_clock::now().time_since_epoch().count()),
                  feature_distribution_(0, feature_list->size() - 1), delay_distribution_(500, 1500)
            {
                stub->async()->RecordRoute(&context_, &stats_, this);
                // Use a hold since some StartWrites are invoked indirectly from a
                // delayed lambda in OnWriteDone rather than directly from the reaction
                // itself
                AddHold();
                NextWrite();
                StartCall();
            }
            void OnWriteDone(bool ok) override
            {
                // Delay and then do the next write or WritesDone
                alarm_.Set(std::chrono::system_clock::now() +
                               std::chrono::milliseconds(delay_distribution_(generator_)),
                           [this](bool /*ok*/) { NextWrite(); });
            }
            void OnDone(const grpc::Status &s) override
            {
                std::unique_lock<std::mutex> l(mu_);
                status_ = s;
                done_ = true;
                cv_.notify_one();
            }
            grpc::Status Await(routeguide::RouteSummary *stats)
            {
                std::unique_lock<std::mutex> l(mu_);
                cv_.wait(l, [this] { return done_; });
                *stats = stats_;
                return std::move(status_);
            }

          private:
            void NextWrite()
            {
                if (points_remaining_ != 0)
                {
                    const routeguide::Feature &f = (*feature_list_)[feature_distribution_(generator_)];
                    std::cout << "Visiting point " << f.location().latitude() / coord_factor_ << ", "
                              << f.location().longitude() / coord_factor_ << std::endl;
                    StartWrite(&f.location());
                    points_remaining_--;
                }
                else
                {
                    StartWritesDone();
                    RemoveHold();
                }
            }
            grpc::ClientContext context_;
            float coord_factor_;
            int points_remaining_ = 10;
            routeguide::Point point_;
            routeguide::RouteSummary stats_;
            const std::vector<routeguide::Feature> *feature_list_;
            std::default_random_engine generator_;
            std::uniform_int_distribution<int> feature_distribution_;
            std::uniform_int_distribution<int> delay_distribution_;
            grpc::Alarm alarm_;
            std::mutex mu_;
            std::condition_variable cv_;
            grpc::Status status_;
            bool done_ = false;
        };
        Recorder recorder(stub_.get(), kCoordFactor_, &feature_list_);
        routeguide::RouteSummary stats;
        grpc::Status status = recorder.Await(&stats);
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
        class Chatter : public grpc::ClientBidiReactor<routeguide::RouteNote, routeguide::RouteNote>
        {
          public:
            explicit Chatter(routeguide::RouteGuide::Stub *stub)
                : notes_{MakeRouteNote("First message", 0, 0), MakeRouteNote("Second message", 0, 1),
                         MakeRouteNote("Third message", 1, 0), MakeRouteNote("Fourth message", 0, 0)},
                  notes_iterator_(notes_.begin())
            {
                stub->async()->RouteChat(&context_, this);
                NextWrite();
                StartRead(&server_note_);
                StartCall();
            }
            void OnWriteDone(bool /*ok*/) override
            {
                NextWrite();
            }
            void OnReadDone(bool ok) override
            {
                if (ok)
                {
                    std::cout << "Got message " << server_note_.message() << " at "
                              << server_note_.location().latitude() << ", " << server_note_.location().longitude()
                              << std::endl;
                    StartRead(&server_note_);
                }
            }
            void OnDone(const grpc::Status &s) override
            {
                std::unique_lock<std::mutex> l(mu_);
                status_ = s;
                done_ = true;
                cv_.notify_one();
            }
            grpc::Status Await()
            {
                std::unique_lock<std::mutex> l(mu_);
                cv_.wait(l, [this] { return done_; });
                return std::move(status_);
            }

          private:
            void NextWrite()
            {
                if (notes_iterator_ != notes_.end())
                {
                    const auto &note = *notes_iterator_;
                    std::cout << "Sending message " << note.message() << " at " << note.location().latitude() << ", "
                              << note.location().longitude() << std::endl;
                    StartWrite(&note);
                    notes_iterator_++;
                }
                else
                {
                    StartWritesDone();
                }
            }
            grpc::ClientContext context_;
            const std::vector<routeguide::RouteNote> notes_;
            std::vector<routeguide::RouteNote>::const_iterator notes_iterator_;
            routeguide::RouteNote server_note_;
            std::mutex mu_;
            std::condition_variable cv_;
            grpc::Status status_;
            bool done_ = false;
        };

        Chatter chatter(stub_.get());
        grpc::Status status = chatter.Await();
        if (!status.ok())
        {
            std::cout << "RouteChat rpc failed." << std::endl;
        }
    }

  private:
    bool GetOneFeature(const routeguide::Point &point, routeguide::Feature *feature)
    {
        grpc::ClientContext context;
        bool result;
        std::mutex mu;
        std::condition_variable cv;
        bool done = false;
        stub_->async()->GetFeature(
            &context, &point, feature, [&result, &mu, &cv, &done, feature, this](grpc::Status status) {
                bool ret;
                if (!status.ok())
                {
                    std::cout << "GetFeature rpc failed." << std::endl;
                    ret = false;
                }
                else if (!feature->has_location())
                {
                    std::cout << "Server returns incomplete feature." << std::endl;
                    ret = false;
                }
                else if (feature->name().empty())
                {
                    std::cout << "Found no feature at " << feature->location().latitude() / kCoordFactor_ << ", "
                              << feature->location().longitude() / kCoordFactor_ << std::endl;
                    ret = true;
                }
                else
                {
                    std::cout << "Found feature called " << feature->name() << " at "
                              << feature->location().latitude() / kCoordFactor_ << ", "
                              << feature->location().longitude() / kCoordFactor_ << std::endl;
                    ret = true;
                }
                std::lock_guard<std::mutex> lock(mu);
                result = ret;
                done = true;
                cv.notify_one();
            });
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [&done] { return done; });
        return result;
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

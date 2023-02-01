# Google Remote Procedure Call

## Install PROTOC compiler

### Dockerfile

[Reference](https://github.com/leimao/gRPC-Examples/blob/master/docker/grpc.Dockerfile)

<details>
  <summary>Click to expand</summary>

```dockerfile
FROM ubuntu:focal

LABEL maintainer="Lei Mao <dukeleimao@gmail.com>"

ARG GPRC_VERSION=1.46.6
ARG NUM_JOBS=12
ARG OPENSSL_VERSION="1.1.1s"
ARG PROTOBUF_VERSION=21.12
ARG PROTOBUF_PYTHON_VERSION=4.21.12
ENV DEBIAN_FRONTEND noninteractive

# Install package dependencies
RUN apt update && \
    apt install -y --no-install-recommends \
        build-essential \
        software-properties-common \
        autoconf \
        automake \
        libtool \
        pkg-config \
        ca-certificates \
        wget \
        git \
        curl \
        vim \
        gdb \
        zlib1g-dev \
        valgrind \
        libssl-dev \
        libcurl4-openssl-dev \
        nano && \
    apt clean

# Install Cmake from source
RUN cd / && git clone --recurse-submodules https://github.com/Kitware/CMake.git && \
    cd CMake && ./bootstrap --system-curl --parallel=${NUM_JOBS} && \
    make -j${NUM_JOBS} && make install && \
    cd / && rm -rf CMake*

# gRPC C++ Runtime
# https://github.com/grpc/grpc/tree/master/src/cpp
# https://github.com/grpc/grpc/blob/master/BUILDING.md
RUN cd / && \
    apt-get install -y build-essential autoconf libtool pkg-config
RUN git clone --recurse-submodules -b v${GPRC_VERSION} https://github.com/grpc/grpc && \
    cd grpc && \
    mkdir -p cmake/build && \
    cd cmake/build && \
    cmake -DgRPC_INSTALL=ON \
        -DgRPC_BUILD_TESTS=OFF \
        -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR \
        ../.. && \
    make -j${NUM_JOBS} && \
    make install

# gRPC Python Runtime
RUN apt update && \
    apt install -y --no-install-recommends \
        python3 \
        python3-dev \
        python3-pip \
        python3-setuptools && \
    apt clean

RUN cd /usr/local/bin && \
    ln -sf /usr/bin/python3 python && \
    ln -sf /usr/bin/pip3 pip && \
    python3 -m pip install --upgrade pip setuptools wheel cython coverage

RUN python3 -m pip install tzdata==2022.5

RUN cd /grpc && \
    GRPC_PYTHON_BUILD_WITH_CYTHON=1 GRPC_BUILD_WITH_BORING_SSL_ASM=0 python3 -m pip install .
    #GRPC_PYTHON_BUILD_WITH_CYTHON=1 GRPC_PYTHON_BUILD_SYSTEM_OPENSSL=0 GRPC_BUILD_WITH_BORING_SSL_ASM=0 python3 -m pip install .

WORKDIR /root
```

</details>

### C++

#### Compile `.proto` file

##### [Defining the service](https://grpc.io/docs/languages/cpp/basics/#defining-the-service)

1. To define a service, you specify a named `service` in your `.proto` file:

    ```proto
    service RouteGuide
    {
        rpc ...
        rpc ...
        ...
    }
    ```

2. Then you define `rpc` methods inside your service definition, specifying their request and response types. gRPC lets you define **_four_** kinds of service method, all of which are used in the `RouteGuide` service:

    - A **_simple RPC_** where the client sends a request to the server using the stub and waits for a response to come back, just like a normal function call.

        ```proto
        // Obtains the feature at a given position.
        rpc GetFeature(Point) returns (Feature) {}
        ```

    - A **_server-side streaming RPC_** where the client sends a request to the server and gets a stream to read a sequence of messages back. The client reads from the returned stream until there are no more messages. As you can see in our example, you specify a server-side streaming method by placing the `stream` keyword before the response type.
    
        ```proto
        // Obtains the Features available within the given Rectangle. Results are
        // streamed rather than returned at once (e.g. in a response message with a
        // repeated field), as the rectangle may cover a large area and contain a
        // huge number of features.
        rpc ListFeatures(Rectangle) returns (stream Feature) {}
        ```

    - A **_client-side streaming RPC_** where the client writes a sequence of messages and sends them to the server, again using a provided stream. Once the client has finished writing the messages, it waits for the server to read them all and return its response. You specify a client-side streaming method by placing the `stream` keyword before the request type.

        ```proto
        // Accepts a stream of Points on a route being traversed, returning a
        // RouteSummary when traversal is completed.
        rpc RecordRoute(stream Point) returns (RouteSummary) {}
        ```

    - A **_bidirectional streaming RPC_** where both sides send a sequence of messages using a read-write stream. The two streams operate independently, so clients and servers can read and write in whatever order they like: for example, the server could wait to receive all the client messages before writing its responses, or it could alternately read a message then write a message, or some other combination of reads and writes. The order of messages in each stream is preserved. You specify this type of method by placing the `stream` keyword before both the request and the response.

        ```proto
        // Accepts a stream of RouteNotes sent while a route is being traversed,
        // while receiving other RouteNotes (e.g. from other users).
        rpc RouteChat(stream RouteNote) returns (stream RouteNote) {}
        ```

##### [Generating client and server code](https://grpc.io/docs/languages/cpp/basics/#generating-client-and-server-code)

```bash
find . -name "*.proto" -type f -exec protoc -I=./protoc --cpp_out=./protoc {} \;
find . -name "*.proto" -type f -exec protoc -I=./protoc --grpc_out=./protoc --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` {} \;
```

- Running this command generates the following files in your current directory:

    - `route_guide.pb.h`, the header which declares your generated message classes
    - `route_guide.pb.cc`, which contains the implementation of your message classes
    - `route_guide.grpc.pb.h`, the header which declares your generated service classes
    - `route_guide.grpc.pb.cc`, which contains the implementation of your service classes

- These contain:

    - All the protocol buffer code to populate, serialize, and retrieve our request and response message types

    - A class called `RouteGuide` that contains
        - a remote interface type (or stub) for clients to call with the methods defined in the `RouteGuide` service.
        - two abstract interfaces for servers to implement, also with the methods defined in the `RouteGuide` service.

#### C++ Synchronous API example

##### [Creating the server](https://grpc.io/docs/languages/cpp/basics/#server)

- There are two parts to making our RouteGuide service do its job:

    - Implementing the service interface generated from our service definition: doing the actual “work” of our service.
    - Running a gRPC server to listen for requests from clients and return the service responses.

1. [Implementing RouteGuide](https://grpc.io/docs/languages/cpp/basics/#implementing-routeguide)

<details>
  <summary>Click to expand</summary>

- As you can see, our server has a `RouteGuideImpl` class that implements the generated `RouteGuide::Service` interface. `RouteGuideImpl` implements all our service methods.

    ```cpp
    class RouteGuideImpl final : public RouteGuide::Service
    {
        ...
    }
    ```

- Let’s look at the simplest type first, `GetFeature`, which just gets a `Point` from the client and returns the corresponding feature information from its database in a `Feature`.

    ```cpp
    Status GetFeature(ServerContext* context, const Point* point, Feature* feature) override
    {
        feature->set_name(GetFeatureName(*point, feature_list_));
        feature->mutable_location()->CopyFrom(*point);
        return Status::OK;
    }
    ```

- The method is passed a `context` object for the RPC, the client’s `Point` protocol buffer request, and a `Feature` protocol buffer to fill in with the response information. In the method we populate the `Feature` with the appropriate information, and then `return` with an `OK` status to tell gRPC that we’ve finished dealing with the RPC and that the `Feature` can be returned to the client.

- Note that all service methods can (and will!) be called from multiple threads at the same time. You have to **make sure that your method implementations are thread safe**. In our example, `feature_list_` is never changed after construction, so it is safe by design. But if `feature_list_` would change during the lifetime of the service, we would need to synchronize access to this member.

- Now let’s look at something a bit more complicated - a streaming RPC. `ListFeatures` is a server-side streaming RPC, so we need to send back multiple `Features` to our client.

    ```cpp
    Status ListFeatures(ServerContext* context, const Rectangle* rectangle, ServerWriter<Feature>* writer) override
    {
        auto lo = rectangle->lo();
        auto hi = rectangle->hi();
        long left = std::min(lo.longitude(), hi.longitude());
        long right = std::max(lo.longitude(), hi.longitude());
        long top = std::max(lo.latitude(), hi.latitude());
        long bottom = std::min(lo.latitude(), hi.latitude());
        for (const Feature& f : feature_list_)
        {
            if (f.location().longitude() >= left &&
                f.location().longitude() <= right &&
                f.location().latitude() >= bottom &&
                f.location().latitude() <= top)
            {
                writer->Write(f);
            }
        }
        return Status::OK;
    }
    ```

- As you can see, instead of getting simple request and response objects in our method parameters, this time we get a request object (the `Rectangle` in which our client wants to find `Features`) and a special `ServerWriter` object. In the method, we populate as many `Feature` objects as we need to return, writing them to the `ServerWriter` using its `Write()` method. Finally, as in our simple RPC, we `return Status::OK` to tell gRPC that we’ve finished writing responses.

- If you look at the client-side streaming method `RecordRoute` you’ll see it’s quite similar, except this time we get a `ServerReader` instead of a request object and a single response. We use the `ServerReader`'s `Read()` method to repeatedly read in our client’s requests to a request object (in this case a Point) until there are no more messages: the server needs to check the return value of `Read()` after each call. If true, the stream is still good and it can continue reading; if false the message stream has ended.

    ```cpp
    Status RecordRoute(ServerContext *context, ServerReader<Point>* reader, RouteSummary *summary) override
    {
        ...
        while (stream->Read(&point))
        {
            ...//process client input
        }
    }
    ```

- Finally, let’s look at our bidirectional streaming RPC `RouteChat()`.

    ```cpp
    Status RouteChat(ServerContext* context,
                     ServerReaderWriter<RouteNote, RouteNote>* stream) override
    {
        RouteNote note;
        while (stream->Read(&note))
        {
            std::unique_lock<std::mutex> lock(mu_);
            for (const RouteNote& n : received_notes_)
            {
                if (n.location().latitude() == note.location().latitude() &&
                    n.location().longitude() == note.location().longitude())
                {
                    stream->Write(n);
                }
            }
            received_notes_.push_back(note);
        }

        return Status::OK;
    }
    ```

- This time we get a `ServerReaderWriter` that can be used to read and write messages. The syntax for reading and writing here is exactly the same as for our client-streaming and server-streaming methods. Although each side will always get the other’s messages in the order they were written, both the client and server can read and write in any order — the streams operate completely independently.

- Note that since `received_notes_` is an instance variable and can be accessed by multiple threads, we use a mutex lock here to guarantee exclusive access.

</details>

2. [Starting the server](https://grpc.io/docs/languages/cpp/basics/#starting-the-server)

<details>
  <summary>Click to expand</summary>

- Once we’ve implemented all our methods, we also need to start up a gRPC server so that clients can actually use our service. The following snippet shows how we do this for our `RouteGuide` service:

    ```cpp
    void RunServer(const std::string& db_path)
    {
        std::string server_address("0.0.0.0:50051");
        RouteGuideImpl service(db_path);

        ServerBuilder builder;
        builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
        builder.RegisterService(&service);
        std::unique_ptr<Server> server(builder.BuildAndStart());
        std::cout << "Server listening on " << server_address << std::endl;
        server->Wait();
    }
    ```

- As you can see, we build and start our server using a `ServerBuilder`. To do this, we:

    - Create an instance of our service implementation class `RouteGuideImpl`.
    - Create an instance of the factory `ServerBuilder` class.
    - Specify the address and port we want to use to listen for client requests using the builder’s `AddListeningPort()` method.
    - Register our service implementation with the builder.
    - Call `BuildAndStart()` on the builder to create and start an RPC server for our service.
    - Call `Wait()` on the server to do a blocking wait until process is killed or `Shutdown()` is called.

</details>

##### [Creating the client](https://grpc.io/docs/languages/cpp/basics/#client)

1. [Creating a stub](https://grpc.io/docs/languages/cpp/basics/#creating-a-stub)

<details>
  <summary>Click to expand</summary>

- To call service methods, we first need to create a stub.

- First we need to create a gRPC channel for our stub, specifying the server address and port we want to connect to - in our case we’ll use no SSL:

    ```cpp
    grpc::CreateChannel("localhost:50051", grpc::InsecureChannelCredentials());
    ```

- Now we can use the channel to create our stub using the `NewStub` method provided in the `RouteGuide` class we generated from our `.proto`.

    ```cpp
    public:
        RouteGuideClient(std::shared_ptr<ChannelInterface> channel, const std::string& db)
                    : stub_(RouteGuide::NewStub(channel)) 
        {
            ...
        }
    ```

</details>


2. [Calling service methods](https://grpc.io/docs/languages/cpp/basics/#calling-service-methods)

<details>
  <summary>Click to expand</summary>

- Now let’s look at how we call our service methods. Note that in this tutorial we’re calling the _**blocking/synchronous**_ versions of each method: this means that the RPC call waits for the server to respond, and will either return a response or raise an exception.

- [**_Simple RPC_**](https://grpc.io/docs/languages/cpp/basics/#simple-rpc) - Calling the simple RPC `GetFeature` is nearly as straightforward as calling a local method.

    ```cpp
    Point point;
    Feature feature;
    point = MakePoint(409146138, -746188906);
    GetOneFeature(point, &feature);

    ...

    bool GetOneFeature(const Point& point, Feature* feature)
    {
        ClientContext context;
        Status status = stub_->GetFeature(&context, point, feature);
        ...
    }
    ```

- As you can see, we create and populate a request protocol buffer object (in our case `Point`), and create a response protocol buffer object for the server to fill in. We also create a `ClientContext` object for our call - you can optionally set RPC configuration values on this object, such as deadlines, though for now we’ll use the default settings. Note that you **_cannot reuse this object between calls_**. Finally, we call the method on the stub, passing it the context, request, and response. If the method returns `OK`, then we can read the response information from the server from our response object.

    ```cpp
    std::cout << "Found feature called " << feature->name()  << " at "
              << feature->location().latitude()/kCoordFactor_ << ", "
              << feature->location().longitude()/kCoordFactor_ << std::endl;
    ```

- [**_Streaming RPCs_**](https://grpc.io/docs/languages/cpp/basics/#streaming-rpcs) - Now let’s look at our streaming methods. If you’ve already read Creating the server some of this may look very familiar - streaming RPCs are implemented in a similar way on both sides. Here’s where we call the server-side streaming method `ListFeatures`, which returns a stream of geographical `Feature`s:

    ```cpp
    std::unique_ptr<ClientReader<Feature>> reader(stub_->ListFeatures(&context, rect));
    while (reader->Read(&feature))
    {
        std::cout << "Found feature called "
                  << feature.name() << " at "
                  << feature.location().latitude()/kCoordFactor_ << ", "
                  << feature.location().longitude()/kCoordFactor_ << std::endl;
    }
    Status status = reader->Finish();
    ```

- Instead of passing the method a context, request, and response, we pass it a context and request and get a `ClientReader` object back. The client can use the `ClientReader` to read the server’s responses. We use the `ClientReader`s `Read()` method to repeatedly read in the server’s responses to a response protocol buffer object (in this case a `Feature`) until there are no more messages: the client needs to check the return value of `Read()` after each call. If `true`, the stream is still good and it can continue reading; if `false` the message stream has ended. Finally, we call `Finish()` on the stream to complete the call and get our RPC status.

- The client-side streaming method `RecordRoute` is similar, except there we pass the method a context and response object and get back a `ClientWriter`.

    ```cpp
    std::unique_ptr<ClientWriter<Point>> writer(stub_->RecordRoute(&context, &stats));
    for (int i = 0; i < kPoints; i++)
    {
        const Feature& f = feature_list_[feature_distribution(generator)];
        std::cout << "Visiting point "
                  << f.location().latitude()/kCoordFactor_ << ", "
                  << f.location().longitude()/kCoordFactor_ << std::endl;
        if (!writer->Write(f.location()))
        {
            // Broken stream.
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_distribution(generator)));
        }
        writer->WritesDone();
        Status status = writer->Finish();
        if (status.IsOk())
        {
            std::cout << "Finished trip with " << stats.point_count() << " points\n"
                      << "Passed " << stats.feature_count() << " features\n"
                      << "Travelled " << stats.distance() << " meters\n"
                      << "It took " << stats.elapsed_time() << " seconds\n";
        }
        else
        {
            std::cout << "RecordRoute rpc failed." << std::endl;
        }
    ```

- Once we’ve finished writing our client’s requests to the stream using `Write()`, we need to call `WritesDone()` on the stream to let gRPC know that we’ve finished writing, then `Finish()` to complete the call and get our RPC status. If the status is `OK`, our response object that we initially passed to `RecordRoute()` will be populated with the server’s response.

- Finally, let’s look at our bidirectional streaming RPC `RouteChat()`. In this case, we just pass a context to the method and get back a `ClientReaderWriter`, which we can use to both write and read messages.

    ```cpp
    std::shared_ptr<ClientReaderWriter<RouteNote, RouteNote>> stream(stub_->RouteChat(&context));
    ```

- The syntax for reading and writing here is exactly the same as for our client-streaming and server-streaming methods. Although each side will always get the other’s messages in the order they were written, both the client and server can read and write in any order — the streams operate completely independently.

</details>

#### [C++ Callback-based Asynchronous API example](https://github.com/grpc/proposal/blob/master/L67-cpp-callback-api.md#proposal)

- The callback API is designed to have the performance and thread scalability of an asynchronous API without the burdensome programming model of the completion-queue-based model. In particular, the following are fundamental guiding principles of the API:

    - Library directly **_calls user-specified code at the completion of RPC actions_**. This user code is run from the library's own threads, so it is very important that it **_must not wait for completion_** of any blocking operations (e.g., condition variable waits, invoking synchronous RPCs, blocking file I/O).
    - No explicit polling required for notification of completion of RPC actions.
    - Like the synchronous API and unlike the completion-queue-based asynchronous API, there is no need for the application to "request" new server RPCs. Server RPC context structures will be allocated and have their resources allocated as and when RPCs arrive at the server.

##### [Reactor model](https://github.com/grpc/proposal/blob/master/L67-cpp-callback-api.md#reactor-model)

- The most general form of the callback API is built around a _**reactor**_ model. Each type of RPC has a reactor base class provided by the library. These types are:

    - `ClientUnaryReactor` and `ServerUnaryReactor` for unary RPCs
    - `ClientBidiReactor` and `ServerBidiReactor` for bidi-streaming RPCs
    - `ClientReadReactor` and `ServerWriteReactor` for server-streaming RPCs
    - `ClientWriteReactor` and `ServerReadReactor` for client-streaming RPCs

- Client RPC invocations from a stub provide a reactor pointer as one of their arguments, and the method handler of a server RPC must return a reactor pointer.

<details>
    <summary>Click to expand</summary>
    
- These base classes provide three types of methods:

    1. Operation-initiation methods: start an asynchronous activity in the RPC. These are methods provided by the class and are not virtual. These are invoked by the application logic. All of these have a `void` return type. The `ReadMessageType` below is the request type for a server RPC and the response type for a client RPC; the `WriteMessageType` is the response type for a server RPC or the request type for a client RPC.

        - `void StartCall()`: _(Client only)_ Initiates the operations of a call from the client, including sending any client-side initial metadata associated with the RPC. Must be called exactly once. No reads or writes will actually be started until this is called (i.e., any previous calls to `StartRead`, `StartWrite`, or `StartWritesDone` will be queued until `StartCall` is invoked). This operation is not needed at the server side since streaming operations at the server are released from backlog automatically by the library as soon as the application returns a reactor from the method handler, and because there is a separate method just for sending initial metadata.

        - `void StartSendInitialMetadata()`: _(Server only)_ Sends server-side initial metadata. To be used in cases where initial metadata should be sent without sending a message. Optional; if not called, initial metadata will be sent when `StartWrite` or `Finish` is called. May not be invoked more than once or after `StartWrite` or `Finish` has been called. This does not exist at the client because sending initial metadata is part of `StartCall`.

        - `void StartRead(ReadMessageType*)`: Starts a read of a message into the object pointed to by the argument. `OnReadDone` will be invoked when the read is complete. Only one read may be outstanding at any given time for an RPC (though a read and a write can be concurrent with each other). If this operation is invoked by a client before calling `StartCall` or by a server before returning from the method handler, it will be queued until one of those events happens and will not actually trigger any activity or reactions until it is thereby released from the queue.

        - `void StartWrite(const WriteMessageType*)`: Starts a write of the object pointed to by the argument. `OnWriteDone` will be invoked when the write is complete. Only one write may be outstanding at any given time for an RPC (though a read and a write can be concurrent with each other). As with `StartRead`, if this operation is invoked by a client before calling `StartCall` or by a server before returning from the method handler, it will be queued until one of those events happens and will not actually trigger any activity or reactions until it is thereby released from the queue.

        - `void StartWritesDone()`: _(Client only)_ For client RPCs to indicate that there are no more writes coming in this stream. `OnWritesDoneDone` will be invoked when this operation is complete. This causes future read operations on the server RPC to indicate that there is no more data available. Highly recommended but technically optional; may not be called more than once per call. As with `StartRead` and `StartWrite`, if this operation is invoked by a client before calling `StartCall` or by a server before returning from the method handler, it will be queued until one of those events happens and will not actually trigger any activity or reactions until it is thereby released from the queue.

        - void `Finish(Status)`: _(Server only)_ Sends completion status to the client, asynchronously. Must be called exactly once for all server RPCs, even for those that have already been cancelled. No further operation-initiation methods may be invoked after `Finish`.

    2. Operation-completion reaction methods: notification of completion of asynchronous RPC activity. These are all virtual methods that default to an empty function (i.e., `{}`) but may be overridden by the application's reactor definition. These are invoked by the library. All of these have a `void` return type. Most take a `bool ok` argument to indicate whether the operation completed "normally," as explained below.

        - `void OnReadInitialMetadataDone(bool ok)`: _(Client only)_ Invoked by the library to notify that the server has sent an initial metadata response to a client RPC. If `ok` is true, then the RPC received initial metadata normally. If it is false, there is no initial metadata either because the call has failed or because the call received a trailers-only response (which means that there was no actual message and that any information normally sent in initial metadata has been dispatched instead to trailing metadata, which is allowed in the gRPC HTTP/2 transport protocol). This reaction is automatically invoked by the library for RPCs of all varieties; it is uncommonly used as an application-defined reaction however.

        - `void OnReadDone(bool ok)`: Invoked by the library in response to a `StartRead` operation. The `ok` argument indicates whether a message was read as expected. A false `ok` could mean a failed RPC (e.g., cancellation) or a case where no data is possible because the other side has already ended its writes (e.g., seen at the server-side after the client has called `StartWritesDone`).

        - `void OnWriteDone(bool ok)`: Invoked by the library in response to a `StartWrite` operation. The `ok` argument that indicates whether the write was successfully sent; a false value indicates an RPC failure.

        - `void OnWritesDoneDone(bool ok)`: _(Client only)_ Invoked by the library in response to a `StartWritesDone` operation. The bool `ok` argument that indicates whether the writes-done operation was successfully completed; a false value indicates an RPC failure.

        - `void OnCancel()`: _(Server only)_ Invoked by the library if an RPC is canceled before it has a chance to successfully send status to the client side. The reaction may be used for any cleanup associated with cancellation or to guide the behavior of other parts of the system (e.g., by setting a flag in the service logic associated with this RPC to stop further processing since the RPC won't be able to send outbound data anyway). Note that servers must call `Finish` even for RPCs that have already been canceled as this is required to cleanup all their library state and move them to a state that allows for calling `OnDone`.

        - `void OnDone(const Status&)` at the client, `void OnDone()` at the server: Invoked by the library when all outstanding and required RPC operations are completed for a given RPC. For the client-side, it additionally provides the status of the RPC (either as sent by the server with its `Finish` call or as provided by the library to indicate a failure), in which case the signature is `void OnDone(const Status&)`. The server version has no argument, and thus has a signature of `void OnDone()`. Should be used for any application-level RPC-specific cleanup.

        - **_Thread safety_**: the above calls may take place concurrently, except that `OnDone` will always take place after all other reactions. No further RPC operations are permitted to be issued after `OnDone` is invoked.

        - **IMPORTANT USAGE NOTE** : code in any reaction must not block for an arbitrary amount of time since reactions are executed on a finite-sized, library-controlled threadpool. If any long-term blocking operations (like sleeps, file I/O, synchronous RPCs, or waiting on a condition variable) must be invoked as part of the application logic, then it is important to push that outside the reaction so that the reaction can complete in a timely fashion. One way of doing this is to push that code to a separate application-controlled thread.

    3. RPC completion-prevention methods. These are methods provided by the class and are not virtual. They are only present at the client-side because the completion of a server RPC is clearly requested when the application invokes `Finish`. These methods are invoked by the application logic. All of these have a `void` return type.

        - `void AddHold()`: _(Client only)_ This prevents the RPC from being considered complete (ready for `OnDone`) until each `AddHold` on an RPC's reactor is matched to a corresponding `RemoveHold`. An application uses this operation before it performs any extra-reaction flows, which refers to streaming operations initiated from outside a reaction method. Note that an RPC cannot complete before `StartCall`, so holds are not needed for any extra-reaction flows that take place before `StartCall`. As long as there are any holds present on an RPC, though, it may not have `OnDone` called on it, even if it has already received server status and has no other operations outstanding. May be called 0 or more times on any client RPC.

        - `void AddMultipleHolds(int holds)`: _(Client only)_ Shorthand for holds invocations of `AddHold`.

        - `void RemoveHold()`: _(Client only)_ Removes a hold reference on this client RPC. Must be called exactly as many times as `AddHold` was called on the RPC, and may not be called more times than `AddHold` has been called so far for any RPC. Once all holds have been removed, the server has provided status, and all outstanding or required operations have completed for an RPC, the library will invoke `OnDone` for that RPC.

</details>

#### Format C++ code

```bash
sudo apt-get install clang-format
clang-format -style=microsoft -dump-config > .clang-format
find . -regex '.*\.\(cpp\|hpp\|cu\|c\|h\)$' -exec clang-format -style=file -i {} \;
```

### Python

```bash
find . -name "*.proto" -type f -exec protoc -I=./protoc --python_out=./protoc {} \;
```

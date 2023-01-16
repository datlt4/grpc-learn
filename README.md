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

```bash
find . -name "*.proto" -type f -exec protoc -I=./protoc --cpp_out=./protoc {} \;
find . -name "*.proto" -type f -exec protoc -I=./protoc --grpc_out=./protoc --plugin=protoc-gen-grpc=`which grpc_cpp_plugin` {} \;
```

#### C++ code example

- Let's see some [C++ code](cpp/helloworld/greeter.cpp) and it [`.proto` file](protoc/helloworld.proto).

1. Procedure to create `gRPC` server

- Enable some server plugins.

```c++
grpc::EnableDefaultHealthCheckService(true);
grpc::reflection::InitProtoReflectionServerBuilderPlugin();
```

- Declare `ServerBuilder` object.

```c++
grpc::ServerBuilder builder;
```

- Listen to the given address

```c++
// Listen on the given address without any authentication mechanism.
builder.AddListeningPort(server_address, grpc::InsecureServerCredentials());
```

- Register `service` that will handle the communication with clients.

```c++
GreeterServiceImpl service;
builder.RegisterService(&service);
```

- Assmble the server and wait for the server to shutdown. Note that some other thread must be responsible for shutting down the server for this call to ever return.
```c++
std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
server->Wait();
```

2. Procedure to `gRPC` client send reqeuest and receive reply.

- Create container for the data request and data reply

```c++
// Data we are sending to the server.
helloworld::HelloRequest request;
request.set_name(user);

// Container for the data we expect from the server.
helloworld::HelloReply reply;
```

- Create gRPC context for the client. It could be used to convey extra information to the server and/or tweak certain RPC behaviors.

```c++
grpc::ClientContext context;
```

- The actual RPC.

```c++
grpc::Status status = stub_->SayHello(&context, request, &reply);
```

- Act upon its status.

```
if (status.ok()) {}
else {}
```



#### Format C++ code
```bash
sudo apt-get install clang-format
clang-format -style=microsoft -dump-config > .clang-format
find . -regex '.*\.\(cpp\|hpp\|cu\|c\|h\)' -exec clang-format -style=file -i {} \;
```

### Python

```bash
find . -name "*.proto" -type f -exec protoc -I=./protoc --python_out=./protoc {} \;
```

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
    apt install -y --no-install-recommends build-essential software-properties-common autoconf automake libtool pkg-config \
        ca-certificates wget git curl vim gdb zlib1g-dev valgrind libcurl4-openssl-dev nano && \
    apt clean

# Install Cmake from source
RUN cd / && git clone --recurse-submodules https://github.com/Kitware/CMake.git && \
    cd CMake && ./bootstrap --system-curl --parallel=${NUM_JOBS} && \
    make -j${NUM_JOBS} && make install && \
    cd / && rm -rf CMake*

# Protobuf C++ Runtime
RUN cd / && \
    apt install -y autoconf automake libtool curl make g++ unzip && \
    wget https://github.com/protocolbuffers/protobuf/releases/download/v${PROTOBUF_VERSION}/protobuf-all-${PROTOBUF_VERSION}.tar.gz && \
    tar xvzf protobuf-all-${PROTOBUF_VERSION}.tar.gz && rm -rf protobuf-all-${PROTOBUF_VERSION}.tar.gz && \
    cd protobuf-${PROTOBUF_VERSION} && ./configure && make -j${NUM_JOBS} && make check -j${NUM_JOBS} && make install -j${NUM_JOBS} && \
    ldconfig

# Protobuf Python Runtime
RUN apt update && \
    apt install -y --no-install-recommends \
        python3 python3-dev python3-pip python3-setuptools && \
    apt clean

RUN cd /usr/local/bin && \
    ln -sf /usr/bin/python3 python && \
    ln -sf /usr/bin/pip3 pip && \
    python3 -m pip install --upgrade pip setuptools wheel cython coverage

RUN python3 -m pip install tzdata==2022.5

RUN cd /protobuf-${PROTOBUF_VERSION}/python && \
    python setup.py build && \
    python setup.py test && \
    python setup.py install && \
    python -m pip install .

# gRPC C++ Runtime
# https://github.com/grpc/grpc/tree/master/src/cpp
# https://github.com/grpc/grpc/blob/master/BUILDING.md
RUN cd / && \
    apt-get install -y build-essential autoconf libtool pkg-config
RUN git clone --recurse-submodules -b v${GPRC_VERSION} https://github.com/grpc/grpc && \
    cd grpc && mkdir -p cmake/build && cd cmake/build && \
    cmake -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR ../.. && \
    make -j${NUM_JOBS} && make install

# gRPC Python Runtime
RUN cd /grpc && \
    GRPC_PYTHON_BUILD_WITH_CYTHON=1 GRPC_BUILD_WITH_BORING_SSL_ASM=0 python3 -m pip install .
    #GRPC_PYTHON_BUILD_WITH_CYTHON=1 GRPC_PYTHON_BUILD_SYSTEM_OPENSSL=1 GRPC_BUILD_WITH_BORING_SSL_ASM=0 python3 -m pip install .

WORKDIR /root
```

</details>

### Example

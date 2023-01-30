cmake_minimum_required(VERSION 3.10.2)

project(route_guide LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

# Find Protobuf installation
# Find package
set(protobuf_MODULE_COMPATIBLE TRUE)
find_package(Protobuf REQUIRED)
set(INC ${INC} ${PROTOBUF_INCLUDE_DIR})
set(LIB ${LIB} ${PROTOBUF_LIBRARIES})

# Find gRPC installation
# Looks for gRPCConfig.cmake file installed by gRPC's cmake installation.
find_package(gRPC CONFIG REQUIRED)
message(STATUS "Using gRPC ${gRPC_VERSION}")
set(LIB ${LIB} gRPC::grpc++_reflection)
set(LIB ${LIB} gRPC::grpc++)

set(INC ${INC} "${CMAKE_CURRENT_SOURCE_DIR}/../../protoc/")
file(GLOB PROTO_SRCS "${CMAKE_CURRENT_SOURCE_DIR}/../../protoc/*.cc")
set(SRC ${SRC} ${PROTO_SRCS})

# Include
set(INC ${INC} "${CMAKE_CURRENT_SOURCE_DIR}")
set(INC ${INC} "${CMAKE_CURRENT_SOURCE_DIR}/..")

# App
message(STATUS "INC ${INC}")
message(STATUS "LIB ${LIB}")
set(SRC ${SRC} helper.cpp)

set(APP route_guide)
add_executable(${APP} ${APP}.cpp)
target_sources(${APP} PRIVATE ${SRC})
target_include_directories(${APP} PRIVATE ${INC})
target_link_libraries(${APP} PRIVATE ${LIB})
unset(APP)

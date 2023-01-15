cmake_minimum_required(VERSION 3.10.2)

project(network LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)

# Add sources
set(APP greeter)
add_executable(${APP}_server ${APP}_server.cpp)
target_include_directories(${APP}_server PRIVATE ${INC})
target_link_libraries(${APP}_server PRIVATE ${LIB})
unset(APP)

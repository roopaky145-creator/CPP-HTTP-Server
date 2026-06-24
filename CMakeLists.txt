cmake_minimum_required(VERSION 3.14)
project(http_server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(server src/main.cpp src/ThreadPool.cpp src/HttpRequest.cpp)

find_package(Threads REQUIRED)
target_link_libraries(server Threads::Threads)
target_compile_options(server PRIVATE -O2)

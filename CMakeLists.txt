cmake_minimum_required(VERSION 3.10)
project(TSQueueProject)

set(CMAKE_CXX_STANDARD 17)

# Add the executable and all source files
add_executable(ts_project main.cpp tests.cpp)

# Link the threading library
find_package(Threads REQUIRED)
target_link_libraries(ts_project Threads::Threads)

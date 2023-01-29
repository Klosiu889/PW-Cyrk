cmake_minimum_required(VERSION 3.18)
project(PW_Cyrk)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-std=c++20 -Wall -Wextra -pthread")

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)


function(add_example_program target_name)
    add_executable(${target_name} "${target_name}.cpp" system.hpp system.cpp)
    target_link_libraries(${target_name} Threads::Threads)
endfunction()

add_example_program(demo)

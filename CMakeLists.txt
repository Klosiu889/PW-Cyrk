cmake_minimum_required(VERSION 3.18)
project(PW_Cyrk)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-g -std=c++20 -Wall -Wextra -Werror -pedantic -fsanitize=undefined,thread")

set(THREADS_PREFER_PTHREAD_FLAG TRUE)
find_package(Threads REQUIRED)

add_library(system system.hpp system.cpp)

function(add_example_program target_name)
    add_executable(${target_name} "${target_name}.cpp")
    target_link_libraries(${target_name} Threads::Threads)
    target_link_libraries(system)
endfunction()

add_example_program(demo)

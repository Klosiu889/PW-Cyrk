cmake_minimum_required(VERSION 3.24)
project(PW_Cyrk)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "-std=c++20 -Wall -Wextra -Werror -pedantic -fsanitize=undefined,thread")

add_executable(PW_Cyrk
        demo.cpp
        machine.hpp
        system.cpp
        system.hpp)

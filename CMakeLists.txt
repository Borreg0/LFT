cmake_minimum_required(VERSION 4.0)
project(Projects C)

set(CMAKE_C_STANDARD 11)

include_directories(.)

add_executable(Projects
        Main.c
        Main.h)

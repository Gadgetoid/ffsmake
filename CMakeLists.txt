cmake_minimum_required(VERSION 3.31)

project(ffsmake)

add_executable(ffsmake
    main.c
    src/ff.c
    src/ffsystem.c
    src/ffunicode.c
)

target_include_directories(ffsmake PRIVATE
    .
    src/)

target_compile_definitions(ffsmake PRIVATE
    FFCONF_H="ffconf-micropython.h"
)

target_compile_options(ffsmake PRIVATE
    -Wall
)
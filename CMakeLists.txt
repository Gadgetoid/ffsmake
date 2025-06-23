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
    src/
)

if(APPLE)
    # find these with: brew ls argp-standalone
    target_include_directories(ffsmake PRIVATE
        /opt/homebrew/Cellar/argp-standalone/1.5.0/include/
    )
    target_link_directories(ffsmake PRIVATE
        /opt/homebrew/Cellar/argp-standalone/1.5.0/lib/
    )
    target_link_libraries(ffsmake PRIVATE argp.a)
endif()

target_compile_definitions(ffsmake PRIVATE
    FFCONF_H="ffconf-micropython.h"
)

target_compile_options(ffsmake PRIVATE
    -Wall
)
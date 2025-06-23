cmake_minimum_required(VERSION 3.25)

project(ffsmake)

add_executable(ffsmake
    main.c
    oofatfs/src/ff.c
    oofatfs/src/ffsystem.c
    oofatfs/src/ffunicode.c
)

target_include_directories(ffsmake PRIVATE
    .
    oofatfs/src/
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

# Enable SUSv1 for nftw support
target_compile_definitions(ffsmake PRIVATE
    FFCONF_H="ffconf-micropython.h"
    _XOPEN_SOURCE
    _XOPEN_SOURCE_EXTENDED
)

target_compile_options(ffsmake PRIVATE
    -Wall
)

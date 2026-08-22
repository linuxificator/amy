cmake_minimum_required(VERSION 3.22.1)
project(amy_android LANGUAGES C CXX)

find_package(oboe REQUIRED CONFIG)

set(AMY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
set(AMY_SRC "${AMY_ROOT}/src")

set(AMY_SOURCES
    ${AMY_SRC}/algorithms.c
    ${AMY_SRC}/amy.c
    ${AMY_SRC}/amy_unix_socket.c
    ${AMY_SRC}/delay.c
    ${AMY_SRC}/envelope.c
    ${AMY_SRC}/filters.c
    ${AMY_SRC}/parse.c
    ${AMY_SRC}/sequencer.c
    ${AMY_SRC}/transfer.c
    ${AMY_SRC}/midi_mappings.c
    ${AMY_SRC}/custom.c
    ${AMY_SRC}/patches.c
    ${AMY_SRC}/oscillators.c
    ${AMY_SRC}/interp_partials.c
    ${AMY_SRC}/pcm.c
    ${AMY_SRC}/log2_exp2.c
    ${AMY_SRC}/instrument.c
    ${AMY_SRC}/amy_midi.c
    ${AMY_SRC}/api.c
    ${AMY_SRC}/cv_trigger.c
)

add_library(amy_android SHARED
    amy_android.cpp
    ${AMY_SOURCES}
)

target_include_directories(amy_android PRIVATE
    ${AMY_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# AMY_DAISY currently selects AMY's existing 48 kHz / 128-frame build profile.
# No Daisy platform code is linked; AMY_NO_MINIAUDIO makes Oboe the sole audio
# backend. AMY_ANDROID is available for future Android-specific compile guards.
target_compile_definitions(amy_android PRIVATE
    AMY_ANDROID=1
    AMY_DAISY=1
    AMY_NO_MINIAUDIO=1
    AMY_WAVETABLE=1
)

target_compile_options(amy_android PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-O3;-Wall;-Wextra;-Wno-unused-parameter;-Wno-float-conversion>
    $<$<COMPILE_LANGUAGE:CXX>:-O3;-Wall;-Wextra;-Wno-unused-parameter>
)

target_compile_features(amy_android PRIVATE c_std_11 cxx_std_17)

target_link_libraries(amy_android PRIVATE
    oboe::oboe
    android
    log
    m
)

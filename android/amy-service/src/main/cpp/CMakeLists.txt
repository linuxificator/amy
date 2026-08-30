cmake_minimum_required(VERSION 3.22.1)
project(amy_android LANGUAGES C CXX)

find_package(oboe REQUIRED CONFIG)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(AMY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../..")
set(AMY_SRC "${AMY_ROOT}/src")
set(GAMMA9001_PCM_C "${CMAKE_CURRENT_BINARY_DIR}/drums_bin.c")
file(GLOB_RECURSE GAMMA9001_PCM_INPUTS CONFIGURE_DEPENDS
    "${AMY_ROOT}/sounds/gamma9001/*.wav"
)

# Generate the large linkable sample blob inside each ABI's private build
# directory.  The tracked maps remain source inputs; parallel Android ABI
# builds never rewrite or share a generated C file in the checkout.
add_custom_command(
    OUTPUT "${GAMMA9001_PCM_C}"
    COMMAND "${Python3_EXECUTABLE}" -m amy.headers
            gamma9001-blob-c "${GAMMA9001_PCM_C}"
    WORKING_DIRECTORY "${AMY_ROOT}"
    DEPENDS
        "${AMY_ROOT}/amy/headers.py"
        "${AMY_ROOT}/sounds/gamma9001/manifest.json"
        ${GAMMA9001_PCM_INPUTS}
    COMMENT "Generating Gamma9001 PCM blob"
    VERBATIM
)
set_source_files_properties("${GAMMA9001_PCM_C}" PROPERTIES GENERATED TRUE)

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
    amy_android_capture.cpp
    amy_android_profile.cpp
    ${GAMMA9001_PCM_C}
    ${AMY_SOURCES}
)

target_include_directories(amy_android PRIVATE
    ${AMY_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# AMY_DAISY selects AMY's existing 48 kHz / 128-frame compile profile. Android
# owns both the audio and MIDI device layers, so no Daisy device implementation
# is linked: AMY_NO_MINIAUDIO leaves Oboe as the sole audio backend and
# AMY_HOST_MIDI leaves run_midi/stop_midi/midi_out to amy_android.cpp.
# delay.c already provides qspi_malloc/qspi_free under AMY_DAISY. pcm.c needs
# declarations for those helpers, so force only the compatibility declarations
# into C translation units; do not link a second allocator implementation.
target_compile_definitions(amy_android PRIVATE
    AMY_ANDROID=1
    AMY_DAISY=1
    AMY_HOST_MIDI=1
    AMY_NO_MINIAUDIO=1
    AMY_WAVETABLE=1
    GAMMA9001=1
)

target_compile_options(amy_android PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include;${CMAKE_CURRENT_SOURCE_DIR}/amy_android_daisy_alloc.h;-O3;-Wall;-Wextra;-Wno-unused-parameter;-Wno-float-conversion>
    $<$<COMPILE_LANGUAGE:CXX>:-O3;-Wall;-Wextra;-Wno-unused-parameter>
)

target_compile_features(amy_android PRIVATE c_std_11 cxx_std_17)

target_link_libraries(amy_android PRIVATE
    oboe::oboe
    android
    log
    m
)

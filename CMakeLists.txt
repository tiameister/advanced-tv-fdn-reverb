cmake_minimum_required(VERSION 3.20)
project(TVFDN_DSP LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(tvfdn_dsp STATIC
    DSP/FractionalDelayLine.cpp
    DSP/TVFDNEngine.cpp
)

target_include_directories(tvfdn_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_options(tvfdn_dsp PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>
)

add_executable(tvfdn_smoke_test Tests/smoke_test.cpp)
target_link_libraries(tvfdn_smoke_test PRIVATE tvfdn_dsp)

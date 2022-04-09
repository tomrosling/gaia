cmake_minimum_required (VERSION 3.16)

add_library(stb_perlin STATIC "./stb_perlin.c")
target_include_directories(stb_perlin PUBLIC . "../../dependencies/stb")


# Enable optimisation for stb_perlin implementation even in Debug because it's a huge bottleneck.
# Also have to remove /RTC1 because it conflicts with /O2.
# https://stackoverflow.com/questions/8587764/remove-runtime-checks-compiler-flag-per-project-in-cmake
string(REGEX REPLACE "/RTC[^ ]*" "" CMAKE_C_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG})
set_target_properties(stb_perlin PROPERTIES COMPILE_FLAGS "-O2 -Ob2")

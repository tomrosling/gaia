cmake_minimum_required (VERSION 3.16)

# Get the CMake built-in projects out of the way
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(gaia)

add_subdirectory(src/gaia)
add_subdirectory(src/gaia_testbed)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT gaia_testbed)

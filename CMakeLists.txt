cmake_minimum_required (VERSION 3.0)

project(gaia)

add_executable(gaia WIN32 src/main.cpp)
if(MSVC)
    # Warnings as errors
    target_compile_options(gaia PRIVATE /WX)

    # Make MSVC stick to the C++ spec
    target_compile_options(${PROJECT_NAME} PRIVATE /permissive-)
endif()

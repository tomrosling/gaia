cmake_minimum_required (VERSION 3.0)

project(gaia)

file(GLOB sources src/*.cpp)
file(GLOB headers src/*.hpp)
include_directories(src)
add_executable(gaia WIN32 ${sources} ${headers})
target_link_libraries(gaia d3d12.lib dxgi.lib d3dcompiler.lib)
target_include_directories(gaia PRIVATE thirdparty/include)
if(MSVC)
    # Warnings as errors
    target_compile_options(gaia PRIVATE /WX)

    # Make MSVC stick to the C++ spec
    target_compile_options(${PROJECT_NAME} PRIVATE /permissive-)
endif()

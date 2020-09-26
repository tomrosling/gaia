cmake_minimum_required (VERSION 3.16)

# Get the CMake built-in projects out of the way
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(gaia)

set(CMAKE_CXX_STANDARD 17)

file(GLOB sources "src/*.cpp")
file(GLOB headers "src/*.hpp")
file(GLOB shaders "data/shaders/*.hlsl")
set_source_files_properties("data/shaders/TerrainVertex.hlsl" "data/shaders/DebugVertex.hlsl" PROPERTIES VS_SHADER_TYPE "Vertex")
set_source_files_properties("data/shaders/TerrainPixel.hlsl" "data/Shaders/DebugPixel.hlsl" PROPERTIES VS_SHADER_TYPE "Pixel")
set_source_files_properties(${shaders} PROPERTIES VS_SHADER_MODEL "5.1")
source_group("Shaders" FILES ${shaders})

# Add DirectXTex helpers.
set(sources ${sources} "dependencies/DirectXTex/WICTextureLoader/WICTextureLoader12.cpp")
set(headers ${headers} "dependencies/DirectXTex/WICTextureLoader/WICTextureLoader12.h")

add_executable(gaia WIN32 ${sources} ${headers} ${shaders})
target_link_libraries(gaia 
    d3d12.lib
    dxgi.lib
    d3dcompiler.lib
    dxguid.lib # For DirectXTex
)
target_include_directories(gaia PRIVATE src 
    "dependencies/d3dx12"
    "dependencies/glm" 
    "dependencies/stb" 
    "dependencies/DirectXTex/WICTextureLoader"
)
target_precompile_headers(gaia PRIVATE src/pch.hpp)

set_property(TARGET gaia PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "\$(ProjectDir)/\$(Configuration)")

# Create symlinks for shader source to allow hotloading.
foreach(config ${CMAKE_CONFIGURATION_TYPES})
  set(outputDir "${CMAKE_CURRENT_BINARY_DIR}/${config}")
  execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${outputDir})
  execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/TerrainPixel.hlsl" "${outputDir}/TerrainPixel.hlsl")
  execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/TerrainVertex.hlsl" "${outputDir}/TerrainVertex.hlsl")
endforeach()

if(MSVC)
    # Warnings as errors
    target_compile_options(gaia PRIVATE /WX)

    # Make MSVC stick to the C++ spec
    target_compile_options(gaia PRIVATE /permissive-)
endif()

cmake_minimum_required (VERSION 3.16)

# Get the CMake built-in projects out of the way
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(gaia)

file(GLOB sources "src/*.cpp")
file(GLOB headers "src/*.hpp")
file(GLOB shaders "data/shaders/*.hlsl")
set_source_files_properties("data/shaders/vertex.hlsl" PROPERTIES VS_SHADER_TYPE "Vertex")
set_source_files_properties("data/shaders/pixel.hlsl" PROPERTIES VS_SHADER_TYPE "Pixel")
set_source_files_properties(${shaders} PROPERTIES VS_SHADER_MODEL "5.1")
source_group("Shaders" FILES ${shaders})

add_executable(gaia WIN32 ${sources} ${headers} ${shaders})
target_link_libraries(gaia d3d12.lib dxgi.lib d3dcompiler.lib)
target_include_directories(gaia PRIVATE src "thirdparty/include")
target_precompile_headers(gaia PRIVATE src/pch.hpp)

# Create symlinks for shader source to allow hotloading.
foreach(config ${CMAKE_CONFIGURATION_TYPES})
  set(outputDir "${CMAKE_CURRENT_BINARY_DIR}/${config}")
  execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${outputDir})
  execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/pixel.hlsl" "${outputDir}/pixel.hlsl")
  execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
    "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/vertex.hlsl" "${outputDir}/vertex.hlsl")
endforeach()

if(MSVC)
    # Warnings as errors
    target_compile_options(gaia PRIVATE /WX)

    # Make MSVC stick to the C++ spec
    target_compile_options(gaia PRIVATE /permissive-)
endif()

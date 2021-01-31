cmake_minimum_required (VERSION 3.16)

# Get the CMake built-in projects out of the way
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

project(gaia)

set(CMAKE_CXX_STANDARD 17)

file(GLOB sources "src/*.cpp")
file(GLOB headers "src/*.hpp")
file(GLOB shaders "data/shaders/*.hlsl")
set_source_files_properties("data/shaders/SkyboxVertex.hlsl" "data/shaders/TerrainVertex.hlsl" "data/shaders/WaterVertex.hlsl" "data/shaders/DebugVertex.hlsl" PROPERTIES VS_SHADER_TYPE "Vertex")
set_source_files_properties("data/shaders/SkyboxPixel.hlsl" "data/shaders/TerrainPixel.hlsl" "data/shaders/WaterPixel.hlsl" "data/Shaders/DebugPixel.hlsl" PROPERTIES VS_SHADER_TYPE "Pixel")
set_source_files_properties("data/shaders/GenerateMips.hlsl" PROPERTIES VS_SHADER_TYPE "Compute")
set_source_files_properties(${shaders} PROPERTIES
    VS_SHADER_MODEL "5.1"
    VS_SHADER_ENABLE_DEBUG true
    #VS_SHADER_DISABLE_OPTIMIZATIONS true
)
source_group("Shaders" FILES ${shaders})

# Add DirectXTex helpers.
# TODO: Tidy this up.
set(sources ${sources} "dependencies/DirectXTex/DirectXTex/DirectXTexUtil.cpp"
                       "dependencies/DirectXTex/DDSTextureLoader/DDSTextureLoader12.cpp"
                       "dependencies/DirectXTex/WICTextureLoader/WICTextureLoader12.cpp")
set(headers ${headers} "dependencies/DirectXTex/DirectXTex/DirectXTex.h"
                       "dependencies/DirectXTex/DDSTextureLoader/DDSTextureLoader12.h"
                       "dependencies/DirectXTex/WICTextureLoader/WICTextureLoader12.h")

# Imgui.
set(sources ${sources} "dependencies/imgui/imgui.cpp"
                       "dependencies/imgui/imgui_draw.cpp"
                       "dependencies/imgui/imgui_widgets.cpp"
                       "dependencies/imgui/examples/imgui_impl_dx12.cpp"
                       "dependencies/imgui/examples/imgui_impl_win32.cpp")
set(headers ${headers} "dependencies/imgui/imgui.h")

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
    "dependencies/imgui"
    "dependencies/stb" 
    "dependencies/DirectXTex"
)
target_precompile_headers(gaia PRIVATE src/pch.hpp)

set_property(TARGET gaia PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "\$(ProjectDir)/\$(Configuration)")

foreach(config ${CMAKE_CONFIGURATION_TYPES})
    set(outputDir "${CMAKE_CURRENT_BINARY_DIR}/${config}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${outputDir})
    
    # Symlink texture data to working directories.
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${CMAKE_CURRENT_SOURCE_DIR}/data/textures/aerial_grass_rock_diff_1k.png" "${outputDir}/aerial_grass_rock_diff_1k.png")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${CMAKE_CURRENT_SOURCE_DIR}/data/textures/ground_grey_diff_1k.png" "${outputDir}/ground_grey_diff_1k.png")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${CMAKE_CURRENT_SOURCE_DIR}/data/textures/skymap.dds" "${outputDir}/skymap.dds")

    # Create symlinks for shader source to allow hotloading.
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/TerrainPixel.hlsl" "${outputDir}/TerrainPixel.hlsl")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/TerrainVertex.hlsl" "${outputDir}/TerrainVertex.hlsl")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/SkyboxPixel.hlsl" "${outputDir}/SkyboxPixel.hlsl")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/SkyboxVertex.hlsl" "${outputDir}/SkyboxVertex.hlsl")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/WaterPixel.hlsl" "${outputDir}/WaterPixel.hlsl")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/WaterVertex.hlsl" "${outputDir}/WaterVertex.hlsl")
endforeach()

if(MSVC)
    # Warnings as errors
    target_compile_options(gaia PRIVATE /WX)

    # Make MSVC stick to the C++ spec
    target_compile_options(gaia PRIVATE /permissive-)
endif()

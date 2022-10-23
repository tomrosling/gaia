cmake_minimum_required (VERSION 3.16)

set(CMAKE_CXX_STANDARD 17)

# Force CMake to output exes, dlls, compiled shaders, etc to build/$(Config), rather than build/src/$(Project)/$(Config)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

file(GLOB_RECURSE sources "./*.cpp")
file(GLOB_RECURSE headers "./*.hpp")

##
## Shaders
##
set(shaders_dir "../../data/shaders")
file(GLOB vertex_shaders "${shaders_dir}/*Vertex.hlsl")
file(GLOB pixel_shaders  "${shaders_dir}/*Pixel.hlsl")
file(GLOB hull_shaders   "${shaders_dir}/*Hull.hlsl")
file(GLOB domain_shaders "${shaders_dir}/*Domain.hlsl")
set(compute_shaders      "${shaders_dir}/GenerateMips.hlsl" "${shaders_dir}/TerrainComputeNormals.hlsl")
set(shaders ${vertex_shaders} ${pixel_shaders} ${hull_shaders} ${domain_shaders} ${compute_shaders})

set_source_files_properties(${vertex_shaders}  PROPERTIES VS_SHADER_TYPE "Vertex")
set_source_files_properties(${pixel_shaders}   PROPERTIES VS_SHADER_TYPE "Pixel")
set_source_files_properties(${hull_shaders}    PROPERTIES VS_SHADER_TYPE "Hull")
set_source_files_properties(${domain_shaders}  PROPERTIES VS_SHADER_TYPE "Domain")
set_source_files_properties(${compute_shaders} PROPERTIES VS_SHADER_TYPE "Compute")
set_source_files_properties(${shaders} PROPERTIES
    VS_SHADER_MODEL "5.1"
    VS_SHADER_ENABLE_DEBUG true
    #VS_SHADER_DISABLE_OPTIMIZATIONS true
    VS_SHADER_FLAGS "/WX"
)

# Add DirectXTex helpers.
# TODO: Tidy this up.
set(dependencies_dir "${CMAKE_CURRENT_LIST_DIR}/../../dependencies")
set(sources ${sources} "${dependencies_dir}/DirectXTex/DirectXTex/DirectXTexUtil.cpp"
                       "${dependencies_dir}/DirectXTex/DDSTextureLoader/DDSTextureLoader12.cpp"
                       "${dependencies_dir}/DirectXTex/WICTextureLoader/WICTextureLoader12.cpp")
set(headers ${headers} "${dependencies_dir}/DirectXTex/DirectXTex/DirectXTex.h"
                       "${dependencies_dir}/DirectXTex/DDSTextureLoader/DDSTextureLoader12.h"
                       "${dependencies_dir}/DirectXTex/WICTextureLoader/WICTextureLoader12.h")

# Imgui.
set(sources ${sources} "${dependencies_dir}/imgui/imgui.cpp"
                       "${dependencies_dir}/imgui/imgui_draw.cpp"
                       "${dependencies_dir}/imgui/imgui_widgets.cpp"
                       "${dependencies_dir}/imgui/examples/imgui_impl_dx12.cpp"
                       "${dependencies_dir}/imgui/examples/imgui_impl_win32.cpp")
set(headers ${headers} "${dependencies_dir}/imgui/imgui.h")

source_group(TREE "${CMAKE_CURRENT_LIST_DIR}/../.." FILES ${sources} ${headers})
source_group(TREE "${CMAKE_CURRENT_LIST_DIR}/../../data" FILES ${shaders})

add_library(gaia STATIC ${sources} ${headers} ${shaders})
target_link_libraries(gaia 
    d3d12.lib
    dxgi.lib
    d3dcompiler.lib
    dxguid.lib # For DirectXTex
    stb_perlin
)
target_include_directories(gaia PUBLIC .
    "${dependencies_dir}/d3dx12"
    "${dependencies_dir}/glm" 
    "${dependencies_dir}/imgui"
    "${dependencies_dir}/stb" 
    "${dependencies_dir}/DirectXTex"
)
target_precompile_headers(gaia PUBLIC pch.hpp)

if(MSVC)
    # Warnings as errors
    target_compile_options(gaia PUBLIC /WX)

    # Make MSVC stick to the C++ spec
    target_compile_options(gaia PUBLIC /permissive-)
endif()

##
## Data build
##
set(data_dir "${CMAKE_CURRENT_LIST_DIR}/../../data")
foreach(config ${CMAKE_CONFIGURATION_TYPES})
    set(outputDir "${CMAKE_BINARY_DIR}/${config}")
    execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${outputDir})
    
    # Symlink texture data to working directories.
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${data_dir}/textures/aerial_grass_rock_diff_4k.dds" "${outputDir}/aerial_grass_rock_diff_4k.dds")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${data_dir}/textures/ground_grey_diff_4k.dds" "${outputDir}/ground_grey_diff_4k.dds")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${data_dir}/textures/aerial_grass_rock_nor_dx_2k.dds" "${outputDir}/aerial_grass_rock_nor_dx_2k.dds")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${data_dir}/textures/ground_grey_nor_dx_2k.dds" "${outputDir}/ground_grey_nor_dx_2k.dds")
    execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
      "${data_dir}/textures/skymap.dds" "${outputDir}/skymap.dds")

    # Create symlinks for shader source to allow hotloading.
    foreach(filepath ${shaders})
        get_filename_component(baseName ${filepath} NAME)
        execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink
          "${filepath}" "${outputDir}/${baseName}")
    endforeach()
endforeach()

cmake_minimum_required (VERSION 3.16)

file(GLOB sources "./*.cpp")
file(GLOB headers "./*.hpp")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${sources} ${headers})

add_executable(gaia_testbed WIN32 ${sources} ${headers})
target_link_libraries(gaia_testbed gaia)
target_include_directories(gaia_testbed PRIVATE src)
set_property(TARGET gaia_testbed PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/\$(Configuration)")

cmake_minimum_required(VERSION 3.5)
set(CXX_STANDARD 11)
project(parallel-PSX LANGUAGES CXX C)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wno-missing-field-initializers")
add_executable(atlas main.cpp atlas.cpp)

add_subdirectory(vulkan)
target_link_libraries(atlas vulkan)

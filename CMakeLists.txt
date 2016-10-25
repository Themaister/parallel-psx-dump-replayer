cmake_minimum_required(VERSION 3.5)
set(CXX_STANDARD 11)
project(parallel-PSX LANGUAGES CXX C)

if (CMAKE_COMPILER_IS_GNUCXX)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Wno-missing-field-initializers")
endif()

add_subdirectory(vulkan)
add_subdirectory(glsl)
add_subdirectory(atlas)
add_subdirectory(renderer)

add_executable(toplevel main.cpp)
target_include_directories(toplevel PRIVATE ${CMAKE_BINARY_DIR}/glsl)

add_dependencies(toplevel shaders)
target_link_libraries(toplevel renderer)


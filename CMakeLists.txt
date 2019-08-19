cmake_minimum_required(VERSION 3.5)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 99)
project(parallel-PSX LANGUAGES CXX C)

if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
    set(PARALLEL_PSX_CXX_FLAGS -Wshadow -Wall -Wextra -Wno-comment -Wno-missing-field-initializers -Wno-empty-body -ffast-math)
    if (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
        set(PARALLEL_PSX_CXX_FLAGS ${PARALLEL_PSX_CXX_FLAGS} -Wno-backslash-newline-escape)
    endif()
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
        message("Enabling SSE3 support.")
        set(PARALLEL_PSX_CXX_FLAGS ${PARALLEL_PSX_CXX_FLAGS} -msse3)
    endif()
elseif (MSVC)
    set(PARALLEL_PSX_CXX_FLAGS /D_CRT_SECURE_NO_WARNINGS /wd4267 /wd4244 /wd4309 /wd4005 /MP /DNOMINMAX)
endif()

# We are overriding settings in subdirectories.
# Avoid warnings.
if (${CMAKE_VERSION} VERSION_GREATER "3.13.0")
    message("Setting CMake policy CMP0077.")
    cmake_policy(SET CMP0077 NEW)
endif()

set(GRANITE_VULKAN_MT OFF CACHE BOOL "No MT needed." FORCE)

add_subdirectory(Granite EXCLUDE_FROM_ALL)
add_library(parallel-psx STATIC
        atlas/atlas.hpp atlas/atlas.cpp
        renderer/renderer.cpp renderer/renderer.hpp)
target_include_directories(parallel-psx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(parallel-psx PUBLIC granite)

add_executable(rsx-player main.cpp)
target_link_libraries(rsx-player PRIVATE parallel-psx)


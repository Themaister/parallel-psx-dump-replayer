add_library(renderer STATIC renderer.cpp texture.cpp)

if (VULKAN_WSI)
   target_link_libraries(renderer vulkan-wsi atlas)
else()
   target_link_libraries(renderer vulkan atlas)
endif()
target_include_directories(renderer PRIVATE ${CMAKE_BINARY_DIR}/glsl)
target_include_directories(renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(renderer shaders)

if (VRAM_ATLAS)
    target_compile_definitions(renderer PUBLIC VRAM_ATLAS=1)
endif()

if (VULKAN_WSI)
   target_compile_definitions(renderer PUBLIC VULKAN_WSI=1)
endif()

add_library(vulkan-symbols
   vulkan_symbol_wrapper.cpp
   )
target_link_libraries(vulkan-symbols)
target_include_directories(vulkan-symbols PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(vulkan
   vulkan.cpp
   device.cpp
   image.cpp
   command_pool.cpp
   fence_manager.cpp
   semaphore_manager.cpp
   command_buffer.cpp
   shader.cpp
   buffer.cpp
   wsi.cpp)

add_subdirectory(external)

find_package(glfw3)
target_link_libraries(vulkan vulkan-symbols vulkan-external glfw)
target_include_directories(vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vulkan PRIVATE HAVE_GLFW VULKAN_DEBUG)

add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL)
target_link_libraries(vulkan spirv-cross-core)

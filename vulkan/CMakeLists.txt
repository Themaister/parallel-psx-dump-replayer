add_library(vulkan
   vulkan.cpp
   vulkan_symbol_wrapper.cpp
   device.cpp
   image.cpp
   command_pool.cpp
   fence_manager.cpp
   semaphore_manager.cpp
   command_buffer.cpp
   memory_allocator.cpp
   wsi.cpp)

find_package(glfw3)
target_link_libraries(vulkan glfw)
target_include_directories(vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vulkan PRIVATE HAVE_GLFW VULKAN_DEBUG)

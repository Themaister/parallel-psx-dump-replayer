add_library(vulkan-symbols
   vulkan_symbol_wrapper.cpp
   )
target_link_libraries(vulkan-symbols)
target_include_directories(vulkan-symbols PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(vulkan
        vulkan.cpp
        device.cpp
        image.cpp
        cookie.cpp
        sampler.cpp
        command_pool.cpp
        fence_manager.cpp
        descriptor_set.cpp
        semaphore_manager.cpp
        command_buffer.cpp
        shader.cpp
        render_pass.cpp
        buffer.cpp
        chain_allocator.cpp
        memory_allocator.cpp
        wsi.cpp)

find_package(glfw3)
target_link_libraries(vulkan vulkan-symbols glfw)
target_include_directories(vulkan PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(vulkan PRIVATE HAVE_GLFW VULKAN_DEBUG)

add_subdirectory(SPIRV-Cross EXCLUDE_FROM_ALL)
target_link_libraries(vulkan spirv-cross-core)

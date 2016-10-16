add_library(vulkan-external
   memory_allocator.cpp)

target_include_directories(vulkan-external PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(vulkan-external vulkan-symbols)

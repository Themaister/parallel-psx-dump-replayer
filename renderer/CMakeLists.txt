add_library(renderer STATIC renderer.cpp)
target_link_libraries(renderer vulkan atlas)
target_include_directories(renderer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

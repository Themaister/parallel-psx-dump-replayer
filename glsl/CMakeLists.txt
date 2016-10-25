add_custom_target(shaders)

set(GLSL_FLAGS -mfmt=c)
function(add_shader OUTPUT INPUT)
   add_custom_command(
      OUTPUT ${CMAKE_BINARY_DIR}/glsl/${OUTPUT}.inc 
      COMMAND glslc -o ${CMAKE_BINARY_DIR}/glsl/${OUTPUT}.inc ${GLSL_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}
      IMPLICIT_DEPENDS CXX ${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}
      VERBATIM)
   add_custom_target(${OUTPUT} DEPENDS ${CMAKE_BINARY_DIR}/glsl/${OUTPUT}.inc)
   set_source_files_properties(${CMAKE_BINARY_DIR}/glsl/${OUTPUT}.inc PROPERTIES GENERATED TRUE)
   add_dependencies(shaders ${OUTPUT})
endfunction(add_shader)

add_shader(quad.vert quad.vert)
add_shader(quad.frag quad.frag)
add_shader(triangle.vert triangle.vert)
add_shader(triangle.frag triangle.frag)
add_shader(test.comp test.comp)


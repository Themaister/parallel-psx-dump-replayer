add_custom_target(shaders)

set(GLSL_FLAGS -mfmt=c)
function(add_shader OUTPUT INPUT)
   add_custom_command(
      OUTPUT ${CMAKE_BINARY_DIR}/glsl/${OUTPUT}.inc 
      COMMAND glslc ${ARGN} -o ${CMAKE_BINARY_DIR}/glsl/${OUTPUT}.inc ${GLSL_FLAGS} ${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}
      DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}
      IMPLICIT_DEPENDS CXX ${CMAKE_CURRENT_SOURCE_DIR}/${INPUT}
      )
   add_custom_target(${OUTPUT} DEPENDS ${CMAKE_BINARY_DIR}/glsl/${OUTPUT}.inc)
   set_source_files_properties(${CMAKE_BINARY_DIR}/glsl/${OUTPUT}.inc PROPERTIES GENERATED TRUE)
   add_dependencies(shaders ${OUTPUT})
endfunction(add_shader)

add_shader(opaque.flat.vert primitive.vert -DFLAT)
add_shader(opaque.flat.frag primitive.frag -DFLAT)
add_shader(opaque.textured.vert primitive.vert -DTEXTURED -DOPAQUE)
add_shader(opaque.textured.frag primitive.frag -DTEXTURED -DOPAQUE)
add_shader(semitrans.opaque.textured.frag primitive.frag -DTEXTURED -DSEMI_TRANS_OPAQUE)
add_shader(semitrans.trans.textured.frag primitive.frag -DTEXTURED -DSEMI_TRANS)
add_shader(resolve.scaled.comp resolve.comp -DSCALED)
add_shader(resolve.unscaled.comp resolve.comp -DUNSCALED)
add_shader(quad.vert quad.vert)
add_shader(unscaled.quad.frag quad.frag -DUNSCALED)
add_shader(scaled.quad.frag quad.frag -DSCALED)
add_shader(triangle.vert triangle.vert)
add_shader(triangle.frag triangle.frag)
add_shader(copy_vram.comp copy_vram.comp)
add_shader(blit_vram.scaled.comp blit_vram.comp -DSCALED)
add_shader(blit_vram.unscaled.comp blit_vram.comp -DUNSCALED)
add_shader(upload.scaled.comp upload.comp -DSCALED)
add_shader(upload.unscaled.comp upload.comp -DUNSCALED)
add_shader(upload.pal4.comp upload.comp -DPAL4)
add_shader(upload.pal8.comp upload.comp -DPAL8)


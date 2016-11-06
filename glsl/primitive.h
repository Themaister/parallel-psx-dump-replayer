#ifndef PRIMITIVE_H
#define PRIMITIVE_H

layout(location = 0) in mediump vec4 vColor;
#ifdef TEXTURED
    #ifdef VRAM_ATLAS
        #include "vram.h"
    #else
        #ifndef SEMI_TRANS
            layout(set = 0, binding = 0) uniform mediump sampler2DArray uTexture;
        #endif
        layout(set = 0, binding = 1) uniform mediump sampler2DArray uTextureNN;
        layout(location = 1) in highp vec3 vUV;
    #endif
#endif
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D uDitherLUT;

vec3 quantize_bgr555(vec3 color)
{
	return round(color * 31.0) / 31.0;
}
#endif
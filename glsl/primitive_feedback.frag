#version 310 es
precision mediump float;

#include "common.h"
#include "primitive.h"

layout(set = 1, binding = 0, input_attachment_index = 0) uniform mediump subpassInput uFeedbackFramebuffer;

void main()
{
#ifdef TEXTURED
    #ifdef VRAM_ATLAS
        vec4 NNColor = sample_vram_atlas(ivec2(vUV));
    #else
        vec4 NNColor = texture(uTextureNN, vUV);
    #endif
        if (all(equal(NNColor, vec4(0.0))))
            discard;

    #ifdef VRAM_ATLAS
        //vec4 color = sample_vram_bilinear(NNColor);
        vec4 color = NNColor;
    #else
        vec4 color = NNColor;
    #endif

    vec3 shaded = color.rgb * vColor.rgb * (255.0 / 128.0);
    float blend_amt = NNColor.a;
#else
    vec3 shaded = vColor.rgb;
    float blend_amt = 1.0;
#endif

    vec4 fbcolor = subpassLoad(uFeedbackFramebuffer);
    if (fbcolor.a > 0.5)
        discard;

#if defined(BLEND_ADD)
    vec3 blended = mix(shaded, shaded + fbcolor.rgb, blend_amt);
#elif defined(BLEND_AVG)
    vec3 blended = mix(shaded, 0.5 * (clamp(shaded, 0.0, 1.0) + fbcolor.rgb), blend_amt);
#elif defined(BLEND_SUB)
    vec3 blended = mix(shaded, fbcolor.rgb - shaded, blend_amt);
#elif defined(BLEND_ADD_QUARTER)
    vec3 blended = mix(shaded, clamp(shaded, 0.0, 1.0) * 0.25 + fbcolor.rgb, blend_amt);
#else
#error "Invalid defines!"
#endif

#ifdef TEXTURED
    FragColor = vec4(blended, NNColor.a + vColor.a);
#else
    FragColor = vec4(blended, vColor.a);
#endif
    FragColor.rgb += textureLod(uDitherLUT, gl_FragCoord.xy * 0.25, 0.0).xxx - 4.0 / 255.0;
    FragColor.rgb = quantize_bgr555(FragColor.rgb);
}
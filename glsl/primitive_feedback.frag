#version 310 es
precision mediump float;

#include "common.h"
#include "primitive.h"

layout(set = 1, binding = 0, input_attachment_index = 0) uniform mediump subpassInput uFeedbackFramebuffer;

void main()
{
#ifdef VRAM_ATLAS
    vec4 NNColor = sample_vram_atlas(ivec2(vUV));
#else
    vec4 NNColor = texture(uTextureNN, vUV);
#endif
    if (all(equal(NNColor, vec4(0.0))))
        discard;

#ifdef VRAM_ATLAS
    vec4 color = sample_vram_bilinear(NNColor);
    //vec4 color = NNColor;
#else
    vec4 color = NNColor;
#endif

    vec3 shaded = color.rgb * vColor.rgb * (255.0 / 128.0);

    vec4 fbcolor = subpassLoad(uFeedbackFramebuffer);
    if (fbcolor.a > 0.5)
        discard;

#if defined(BLEND_ADD)
    vec3 blended = mix(shaded, shaded + fbcolor.rgb, NNColor.a);
#elif defined(BLEND_AVG)
    vec3 blended = mix(shaded, 0.5 * (clamp(shaded, 0.0, 1.0) + fbcolor.rgb), NNColor.a);
#elif defined(BLEND_SUB)
    vec3 blended = mix(shaded, fbcolor.rgb - shaded, NNColor.a);
#elif defined(BLEND_ADD_QUARTER)
    vec3 blended = mix(shaded, clamp(shaded, 0.0, 1.0) * 0.25 + fbcolor.rgb, NNColor.a);
#else
#error "Invalid defines!"
#endif

    FragColor = vec4(blended, NNColor.a + vColor.a);
}
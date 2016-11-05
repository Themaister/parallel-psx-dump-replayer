#version 310 es
precision mediump float;
precision mediump int;

#include "common.h"

layout(location = 0) in mediump vec4 vColor;
#ifdef TEXTURED
    #ifdef VRAM_ATLAS
        layout(location = 1) in highp vec2 vUV;
        layout(location = 2) flat in mediump ivec3 vParam;
        layout(set = 0, binding = 0) uniform mediump usampler2D uFramebuffer;
    #else
        #error ":v"
        #ifndef SEMI_TRANS
            layout(set = 0, binding = 0) uniform mediump sampler2DArray uTexture;
        #endif
        layout(set = 0, binding = 1) uniform mediump sampler2DArray uTextureNN;
        layout(location = 1) in highp vec3 vUV;
    #endif
#endif
layout(location = 0) out vec4 FragColor;

#if defined(VRAM_ATLAS) && defined(TEXTURED)

vec4 sample_vram_atlas(ivec2 uv)
{
    ivec3 params = vParam;
    int shift = params.z;

    ivec2 coord;
    if (shift != 0)
    {
        int bpp = 16 >> shift;
        coord = ivec2(uv);
        int phase = coord.x & ((1 << shift) - 1);
        int align = bpp * phase;
        coord.x >>= shift;
        int value = int(texelFetch(uFramebuffer, coord & ivec2(1023, 511), 0).x);
        int mask = (1 << bpp) - 1;
        value = (value >> align) & mask;

        params.x += value;
        coord = params.xy;
    }
    else
        coord = uv;

    return abgr1555(texelFetch(uFramebuffer, coord & ivec2(1023, 511), 0).x);
}

vec4 sample_vram_bilinear(vec4 NNColor)
{
    vec2 base = vUV;
    ivec2 ibase = ivec2(base);
    vec4 c01 = sample_vram_atlas(ibase + ivec2(0, 1));
    vec4 c10 = sample_vram_atlas(ibase + ivec2(1, 0));
    vec4 c11 = sample_vram_atlas(ibase + ivec2(1, 1));
    float u = fract(base.x);
    float v = fract(base.y);
    vec4 x0 = mix(NNColor, c10, u);
    vec4 x1 = mix(c01, c11, u);
    return mix(x0, x1, v);
}
#endif

void main()
{
#ifdef TEXTURED
    #ifdef VRAM_ATLAS
        vec4 NNColor = sample_vram_atlas(ivec2(vUV));
    #else
        vec4 NNColor = texture(uTextureNN, vUV);
    #endif

        // Even for opaque draw calls, this pixel is transparent.
        // Sample in NN space since we need to do an exact test against 0.0.
        // Doing it in a filtered domain is a bit awkward.
    #ifdef SEMI_TRANS_OPAQUE
        // In this pass, only accept opaque pixels.
        if (all(equal(NNColor, vec4(0.0))) || NNColor.a > 0.5)
            discard;
    #elif defined(OPAQUE) || defined(SEMI_TRANS)
        if (all(equal(NNColor, vec4(0.0))))
            discard;
    #elif !defined(SEMI_TRANS)
    #error "Invalid defines."
    #endif

    #ifdef SEMI_TRANS
        // To avoid opaque pixels from bleeding into the semi-transparent parts,
        // sample nearest-neighbor only in semi-transparent parts of the image.
        vec4 color = NNColor;
    #else
        #ifdef VRAM_ATLAS
            vec4 color = sample_vram_bilinear(NNColor);
            //vec4 color = NNColor;
        #else
            vec4 color = texture(uTexture, vUV);
        #endif
    #endif
        vec3 shaded = color.rgb * vColor.rgb * (255.0 / 128.0);
        FragColor = vec4(shaded, NNColor.a + vColor.a);
#else
    FragColor = vColor;
#endif
}
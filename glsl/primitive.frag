#version 310 es
precision mediump float;

layout(location = 0) in mediump vec4 vColor;
#ifdef TEXTURED
// The UV space for PSX is so small that mediump UV coords is just fine.
layout(location = 1) in mediump vec3 vUV;
#ifndef SEMI_TRANS
layout(set = 0, binding = 0) uniform mediump sampler2DArray uTexture;
#endif
layout(set = 0, binding = 1) uniform mediump sampler2DArray uTextureNN;
#endif
layout(location = 0) out vec4 FragColor;

void main()
{
#ifdef TEXTURED
    vec4 NNColor = texture(uTextureNN, vUV);
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
    vec3 shaded = color.rgb * vColor.rgb * (255.0 / 128.0);
#else
    vec4 color = texture(uTexture, vUV);
    vec3 shaded = color.rgb * vColor.rgb * (255.0 / 128.0);
#endif
    FragColor = vec4(shaded, NNColor.a + vColor.a);
#else
    FragColor = vColor;
#endif
}
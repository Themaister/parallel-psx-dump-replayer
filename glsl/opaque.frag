#version 310 es
precision mediump float;

layout(location = 0) in mediump vec4 vColor;
#ifdef TEXTURED
layout(location = 1) in mediump vec3 vUV;
layout(set = 0, binding = 0) uniform mediump sampler2DArray uTexture;
layout(set = 0, binding = 1) uniform mediump sampler2DArray uTextureNN;
#endif
layout(location = 0) out vec4 FragColor;

void main()
{
#ifdef TEXTURED
    vec4 NNColor = texture(uTextureNN, vUV);
    // Even for opaque draw calls, this pixel is transparent.
    if (all(equal(NNColor, vec4(0.0))))
        discard;

    vec4 color = texture(uTexture, vUV);
    FragColor = vColor * color;
#else
    FragColor = vColor;
#endif
}
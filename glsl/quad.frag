#version 310 es
precision mediump float;

layout(location = 0) in highp vec2 vUV;

#if defined(SCALED)
layout(set = 0, binding = 0) uniform sampler2D uTexture;
#elif defined(UNSCALED)
layout(set = 0, binding = 0) uniform mediump usampler2D uTexture;
#endif
layout(location = 0) out vec4 FragColor;
void main()
{
#if defined(SCALED)
    FragColor = vec4(textureLod(uTexture, vUV, 0.0).rgb, 1.0);
#elif defined(UNSCALED)
    uint value = textureLod(uTexture, vUV, 0.0).x;
    uvec4 color = (uvec4(value) >> uvec4(0u, 5u, 10u, 15u)) & uvec4(31u, 31u, 31u, 1u);
    vec4 unorm = vec4(color) / vec4(31.0, 31.0, 31.0, 1.0);
    FragColor = unorm;
#endif
}
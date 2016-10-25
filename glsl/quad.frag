#version 310 es
precision mediump float;

layout(location = 0) in highp vec2 vUV;
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(location = 0) out vec4 FragColor;
void main()
{
    FragColor = vec4(texture(uTexture, vUV).rgb, 1.0);
}
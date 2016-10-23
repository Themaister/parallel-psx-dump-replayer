#version 310 es
precision mediump float;

layout(set = 0, binding = 0, std140) uniform Register
{
    vec2 dummy;
    vec2 rg;
} registers;

layout(location = 0) out vec4 FragColor;
layout(location = 0) in vec2 vUV;
layout(set = 1, binding = 0) uniform sampler2D uSamp;
void main()
{
   FragColor = texture(uSamp, vUV);
}

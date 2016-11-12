#version 310 es
precision mediump float;

layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(location = 0) in vec2 vUV;

layout(push_constant, std430) uniform Registers
{
   vec2 inv_resolution;
   float lod;
} registers;

void main()
{
   FragColor = textureLod(uTexture, vUV, registers.lod);
}

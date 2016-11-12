#version 310 es
precision mediump float;

layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 0) uniform sampler2D uTexture;
layout(location = 0) in vec2 vUV;

layout(push_constant, std430) uniform Registers
{
   float max_bias;
} registers;

void main()
{
   float b = 0.5 * textureLod(uTexture, vUV, registers.max_bias).a;
   b += 0.125 * textureLodOffset(uTexture, vUV, registers.max_bias, ivec2(-1,  0)).a;
   b += 0.125 * textureLodOffset(uTexture, vUV, registers.max_bias, ivec2(+1,  0)).a;
   b += 0.125 * textureLodOffset(uTexture, vUV, registers.max_bias, ivec2( 0, -1)).a;
   b += 0.125 * textureLodOffset(uTexture, vUV, registers.max_bias, ivec2( 0, +1)).a;

   FragColor = textureLod(uTexture, vUV, registers.max_bias * b);
   //FragColor = vec4(b);
}


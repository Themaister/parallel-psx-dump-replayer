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
   vec2 uv = vUV - 0.25 * registers.inv_resolution;
   vec3 c00 = textureLodOffset(uTexture, uv, registers.lod, ivec2(0, 0)).rgb;
   vec3 c01 = textureLodOffset(uTexture, uv, registers.lod, ivec2(0, 1)).rgb;
   vec3 c10 = textureLodOffset(uTexture, uv, registers.lod, ivec2(1, 0)).rgb;
   vec3 c11 = textureLodOffset(uTexture, uv, registers.lod, ivec2(1, 1)).rgb;

   vec3 avg = 0.25 * (c00 + c01 + c10 + c11);

   // Measure the "energy" in the pixels.
   // If the pixels are all the same (2D content), use maximum bias, otherwise, taper off quickly back to 0 (edges)
   float s00 = dot(c00 - avg, c00 - avg);
   float s01 = dot(c01 - avg, c01 - avg);
   float s10 = dot(c10 - avg, c10 - avg);
   float s11 = dot(c11 - avg, c11 - avg);
   float bias = 1.0 - log2(5000.0 * (s00 + s01 + s10 + s11) + 1.0);

   FragColor = vec4(avg, bias);
}

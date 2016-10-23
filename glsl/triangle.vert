#version 310 es

layout(set = 0, binding = 0, std140) uniform UBO
{
    vec2 offset;
    vec2 dummy;
} registers;

layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 UV;
layout(location = 0) out vec2 vUV;
void main()
{
   gl_Position = Position + vec4(registers.offset, 0.0, 0.0);
   vUV = UV;
}

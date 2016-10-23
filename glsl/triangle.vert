#version 310 es

layout(push_constant, std430) uniform UBO
{
    vec2 offset;
} registers;

layout(location = 0) in vec4 Position;
void main()
{
   gl_Position = Position + vec4(registers.offset, 0.0, 0.0);
}

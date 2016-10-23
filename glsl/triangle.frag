#version 310 es
precision mediump float;

layout(push_constant, std430) uniform Register
{
    vec2 dummy;
    vec2 rg;
} registers;

layout(location = 0) out vec4 FragColor;
void main()
{
   FragColor = vec4(registers.rg, 0.0, 1.0);
}

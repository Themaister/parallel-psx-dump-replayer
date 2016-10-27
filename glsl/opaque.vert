#version 310 es
layout(location = 0) in vec4 Position;
layout(location = 1) in vec4 Color;
layout(location = 0) out mediump vec4 vColor;

const vec2 FB_SIZE = vec2(1024.0, 512.0);

void main()
{
    gl_Position = vec4(Position.xy / FB_SIZE * 2.0 - 1.0, Position.z, 1.0) * Position.w;
    vColor = Color;
}
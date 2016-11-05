#version 310 es
layout(location = 0) in vec4 Position;
layout(location = 1) in vec4 Color;
#ifdef TEXTURED
    layout(location = 2) in vec3 UV;
    layout(location = 1) out vec3 vUV;
    #ifdef VRAM_ATLAS
        layout(location = 3) in mediump ivec3 Param;
        layout(location = 2) flat out mediump ivec3 vParam;
    #endif
#endif
layout(location = 0) out mediump vec4 vColor;

const vec2 FB_SIZE = vec2(1024.0, 512.0);

void main()
{
    gl_Position = vec4(Position.xy / FB_SIZE * 2.0 - 1.0, Position.z, 1.0) * Position.w;
    vColor = Color;
#ifdef TEXTURED
    vUV = UV;
    #ifdef VRAM_ATLAS
        vParam = Param;
    #else
        #error ":v"
    #endif
#endif
}
#version 310 es
layout(location = 0) in vec4 Position;
layout(location = 1) in vec4 Color;
#ifdef TEXTURED
    #ifdef VRAM_ATLAS
        layout(location = 2) in mediump uvec4 Window;
        layout(location = 3) in mediump ivec3 Param;
        layout(location = 4) in ivec4 UV;
        layout(location = 1) out mediump vec2 vUV;
        layout(location = 2) flat out mediump ivec3 vParam;
        layout(location = 3) flat out mediump ivec2 vBaseUV;
        layout(location = 4) flat out mediump ivec4 vWindow;
    #else
        layout(location = 1) out highp vec3 vUV;
        layout(location = 2) in vec3 UV;
    #endif
#endif
layout(location = 0) out mediump vec4 vColor;

const vec2 FB_SIZE = vec2(1024.0, 512.0);

void main()
{
    gl_Position = vec4(Position.xy / FB_SIZE * 2.0 - 1.0, Position.z, 1.0) * Position.w;
    vColor = Color;
#ifdef TEXTURED
    #ifdef VRAM_ATLAS
        vUV = vec2(UV.xy);
        vParam = Param;
        vBaseUV = UV.zw;
        vWindow = ivec4(Window);
    #else
        vUV = UV;
    #endif
#endif
}
#version 310 es
precision mediump float;

layout(location = 0) in mediump vec4 vColor;
#ifdef TEXTURED
layout(location = 1) in mediump vec3 vUV;
layout(set = 0, binding = 0) uniform mediump sampler2DArray uTexture;
#endif
layout(location = 0) out vec4 FragColor;

void main()
{
#ifdef TEXTURED
    FragColor = vColor * texture(uTexture, vUV);
    //FragColor = vec4(0.0);
#else
    FragColor = vColor;
#endif
}
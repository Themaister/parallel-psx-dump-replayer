#version 310 es
precision mediump float;

layout(location = 0) in mediump vec4 vColor;
layout(location = 1) in mediump vec3 vUV;
layout(set = 0, binding = 0, input_attachment_index = 0) uniform mediump subpassInput uFramebuffer;
layout(set = 0, binding = 1) uniform mediump sampler2DArray uTextureNN;
layout(location = 0) out vec4 FragColor;

void main()
{
    vec4 NNColor = texture(uTextureNN, vUV);
    if (all(equal(NNColor, vec4(0.0))))
        discard;

    vec4 color = NNColor;
    vec3 shaded = color.rgb * vColor.rgb * (255.0 / 128.0);

    vec4 fbcolor = subpassLoad(uFramebuffer);
    if (fbcolor.a > 0.5)
        discard;

#if defined(BLEND_ADD)
    vec3 blended = mix(shaded, shaded + fbcolor.rgb, NNColor.a);
#elif defined(BLEND_AVG)
    vec3 blended = mix(shaded, 0.5 * (clamp(shaded, 0.0, 1.0) + fbcolor.rgb), NNColor.a);
#elif defined(BLEND_SUB)
    vec3 blended = mix(shaded, fbcolor.rgb - shaded, NNColor.a);
#elif defined(BLEND_ADD_QUARTER)
    vec3 blended = mix(shaded, clamp(shaded, 0.0, 1.0) * 0.25 + fbcolor.rgb, NNColor.a);
#else
#error "Invalid defines!"
#endif

    FragColor = vec4(blended, NNColor.a + vColor.a);
}
#ifndef COMMON_H
#define COMMON_H

vec4 abgr1555(uint value)
{
   // This rounding is very deliberate, we want 0b11111 to become 0b11111000 in UNORM.
   uvec4 ucolor = (uvec4(value) >> uvec4(0u, 5u, 10u, 15u)) & uvec4(31u, 31u, 31u, 1u);
#define SCALING (8.0 / 255.0)
   return vec4(ucolor) * vec4(SCALING, SCALING, SCALING, 1.0);
}

uvec2 unpack2x16(uint x)
{
   return uvec2(x & 0xffffu, x >> 16u);
}

uvec4 unpack4x8(uint x)
{
   return (uvec4(x) >> uvec4(0u, 8u, 16u, 24u)) & 0xffu;
}

uint pack_abgr1555(vec4 value)
{
   // This rounding is very deliberate, we want 0b11111 to become 0b11111000 in UNORM.
#define INV_SCALING (255.0 / 8.0)
   uvec4 rgba = uvec4(value * vec4(INV_SCALING, INV_SCALING, INV_SCALING, 1.0));
   return (rgba.r << 0u) | (rgba.g << 5u) | (rgba.b << 10u) | (rgba.a << 15u);
}
#endif

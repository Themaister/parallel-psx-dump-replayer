#pragma once

#include "vulkan.hpp"
#include "limits.hpp"
#include "intrusive.hpp"
#include "cookie.hpp"

namespace Vulkan
{
enum RenderPassOp
{
   RENDER_PASS_OP_CLEAR_COLOR_BIT = 1 << 0,
   RENDER_PASS_OP_LOAD_COLOR_BIT = 1 << 1,
   RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT = 1 << 2,
   RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT = 1 << 3,

   RENDER_PASS_OP_STORE_COLOR_BIT = 1 << 4,
   RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT = 1 << 5,

   RENDER_PASS_OP_CLEAR_ALL_BIT =
      RENDER_PASS_OP_CLEAR_COLOR_BIT |
      RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT,

   RENDER_PASS_OP_LOAD_ALL_BIT =
      RENDER_PASS_OP_LOAD_COLOR_BIT |
      RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT,

   RENDER_PASS_OP_STORE_ALL_BIT =
      RENDER_PASS_OP_STORE_COLOR_BIT |
      RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT,

   RENDER_PASS_OP_COLOR_OPTIMAL_BIT = 1 << 6,
   RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT = 1 << 7,
};
using RenderPassOpFlags = uint32_t;

class ImageView;
struct RenderPassInfo
{
   const ImageView *color_attachments[VULKAN_NUM_ATTACHMENTS] = {};
   const ImageView *depth_stencil = nullptr;
   unsigned num_color_attachments = 0;
   RenderPassOpFlags op_flags = 0;
};

class RenderPass : public Cookie
{
public:
	RenderPass(Device *device, const RenderPassInfo &info);
	~RenderPass();

   VkRenderPass get_render_pass() const
   {
      return render_pass;
   }

private:
	Device *device;
	VkRenderPass render_pass = VK_NULL_HANDLE;

   VkFormat color_attachments[VULKAN_NUM_ATTACHMENTS];
   VkFormat depth_stencil;
   unsigned num_color_attachments;
};

class Framebuffer : public Cookie
{
   public:
      Framebuffer(Device *device, const RenderPass &rp, const RenderPassInfo &info);
      ~Framebuffer();

   private:
      Device *device;
      const RenderPass &render_pass;
      RenderPassInfo info;
};
}

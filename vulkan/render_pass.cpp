#include "render_pass.hpp"
#include "device.hpp"
#include <utility>

using namespace std;

namespace Vulkan
{
RenderPass::RenderPass(Device *device, const RenderPassInfo &info)
    : Cookie(device)
    , device(device)
{
	fill(begin(color_attachments), end(color_attachments), VK_FORMAT_UNDEFINED);
	num_color_attachments = info.num_color_attachments;

	VkAttachmentDescription attachments[VULKAN_NUM_ATTACHMENTS + 1];
	unsigned num_attachments = 0;

	VkAttachmentReference color_ref[VULKAN_NUM_ATTACHMENTS];
	VkAttachmentReference ds_ref = { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED };

	VkAttachmentLoadOp color_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	VkAttachmentStoreOp color_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	VkAttachmentLoadOp ds_load_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	VkAttachmentStoreOp ds_store_op = VK_ATTACHMENT_STORE_OP_DONT_CARE;

	if (info.op_flags & RENDER_PASS_OP_CLEAR_COLOR_BIT)
		color_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
	else if (info.op_flags & RENDER_PASS_OP_LOAD_COLOR_BIT)
		color_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;

	if (info.op_flags & RENDER_PASS_OP_STORE_COLOR_BIT)
		color_store_op = VK_ATTACHMENT_STORE_OP_STORE;

	if (info.op_flags & RENDER_PASS_OP_CLEAR_DEPTH_STENCIL_BIT)
		ds_load_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
	else if (info.op_flags & RENDER_PASS_OP_LOAD_DEPTH_STENCIL_BIT)
		ds_load_op = VK_ATTACHMENT_LOAD_OP_LOAD;

	if (info.op_flags & RENDER_PASS_OP_STORE_DEPTH_STENCIL_BIT)
		ds_store_op = VK_ATTACHMENT_STORE_OP_STORE;

	VkImageLayout color_layout = info.op_flags & RENDER_PASS_OP_COLOR_OPTIMAL_BIT ?
	                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL :
	                                 VK_IMAGE_LAYOUT_GENERAL;
	VkImageLayout depth_stencil_layout = info.op_flags & RENDER_PASS_OP_DEPTH_STENCIL_OPTIMAL_BIT ?
	                                         VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
	                                         VK_IMAGE_LAYOUT_GENERAL;

	for (unsigned i = 0; i < info.num_color_attachments; i++)
	{
		color_attachments[i] =
		    info.color_attachments[i] ? info.color_attachments[i]->get_format() : VK_FORMAT_UNDEFINED;

		if (info.color_attachments[i])
		{
			auto &image = info.color_attachments[i]->get_image();
			auto &att = attachments[num_attachments];
			att.flags = 0;
			att.format = color_attachments[i];
			att.samples = image.get_create_info().samples;
			att.loadOp = color_load_op;
			att.storeOp = color_store_op;
			att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			att.initialLayout = color_layout;
			att.finalLayout = color_layout;

			color_ref[i].attachment = num_attachments;
			color_ref[i].layout = color_layout;

			num_attachments++;
		}
		else
		{
			color_ref[i].attachment = VK_ATTACHMENT_UNUSED;
			color_ref[i].layout = VK_IMAGE_LAYOUT_UNDEFINED;
		}
	}

	depth_stencil = info.depth_stencil ? info.depth_stencil->get_format() : VK_FORMAT_UNDEFINED;
	if (info.depth_stencil)
	{
		auto &image = info.depth_stencil->get_image();
		auto &att = attachments[num_attachments];
		att.flags = 0;
		att.format = depth_stencil;
		att.samples = image.get_create_info().samples;
		att.loadOp = ds_load_op;
		att.storeOp = ds_store_op;

		if (format_to_aspect_mask(depth_stencil) & VK_IMAGE_ASPECT_STENCIL_BIT)
		{
			att.stencilLoadOp = ds_load_op;
			att.stencilStoreOp = ds_store_op;
		}
		else
		{
			att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		}

		att.initialLayout = depth_stencil_layout;
		att.finalLayout = depth_stencil_layout;

		ds_ref.attachment = num_attachments;
		ds_ref.layout = depth_stencil_layout;

		num_attachments++;
	}

	VkSubpassDescription subpass = {};
	subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass.colorAttachmentCount = num_color_attachments;
	subpass.pColorAttachments = color_ref;
	subpass.pDepthStencilAttachment = &ds_ref;

	VkRenderPassCreateInfo rp_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
	rp_info.subpassCount = 1;
	rp_info.pSubpasses = &subpass;
	rp_info.pAttachments = attachments;
	rp_info.attachmentCount = num_attachments;

	if (vkCreateRenderPass(device->get_device(), &rp_info, nullptr, &render_pass) != VK_SUCCESS)
		LOG("Failed to create render pass.");
}

RenderPass::~RenderPass()
{
	if (render_pass != VK_NULL_HANDLE)
		vkDestroyRenderPass(device->get_device(), render_pass, nullptr);
}
}

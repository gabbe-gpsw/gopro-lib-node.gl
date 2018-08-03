#include <stdlib.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "backend.h"
#include "format.h"
#include "gctx_vk.h"
#include "log.h"
#include "memory.h"
#include "nodes.h"
#include "rendertarget.h"
#include "texture_vk.h"

int ngli_vk_create_renderpass_info(struct ngl_ctx *ctx, const struct rendertarget_desc *desc, VkRenderPass *render_pass, int conservative)
{
    struct vkcontext *vk = ctx->vkcontext;

    VkAttachmentDescription descs[NGLI_MAX_COLOR_ATTACHMENTS + 1] = {0};
    VkAttachmentReference color_refs[NGLI_MAX_COLOR_ATTACHMENTS] = {0};
    VkAttachmentReference depth_stencil_ref = {0};
    VkAttachmentReference *depth_stencil_refp = NULL;

    for (int i = 0; i < desc->nb_color_formats; i++) {
        VkFormat format = VK_FORMAT_UNDEFINED;
        int ret = ngli_format_get_vk_format(vk, desc->color_formats[i], &format);
        if (ret < 0)
            return ret;
        descs[i] = (VkAttachmentDescription) {
            .format         = format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = conservative ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        color_refs[i].attachment = i;
        color_refs[i].layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    int nb_descs = desc->nb_color_formats;

    if (desc->depth_stencil_format != NGLI_FORMAT_UNDEFINED) {
        VkFormat format = VK_FORMAT_UNDEFINED;
        int ret = ngli_format_get_vk_format(vk, desc->depth_stencil_format, &format);
        if (ret < 0)
            return ret;
        descs[desc->nb_color_formats] = (VkAttachmentDescription) {
            .format         = format,
            .samples        = VK_SAMPLE_COUNT_1_BIT,
            .loadOp         = conservative ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout  = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        depth_stencil_ref.attachment = desc->nb_color_formats;
        depth_stencil_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_stencil_refp = &depth_stencil_ref;
        nb_descs++;
    }

    VkSubpassDescription subpass_description = {
        .pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount    = desc->nb_color_formats,
        .pColorAttachments       = color_refs,
        .pDepthStencilAttachment = depth_stencil_refp,
    };

    VkSubpassDependency dependencies[2] = {
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        }, {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT,
            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT,
        }
    };

    VkRenderPassCreateInfo render_pass_create_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = nb_descs,
        .pAttachments = descs,
        .subpassCount = 1,
        .pSubpasses = &subpass_description,
        .dependencyCount = NGLI_ARRAY_NB(dependencies),
        .pDependencies = dependencies,
    };

    VkResult res = vkCreateRenderPass(vk->device, &render_pass_create_info, NULL, render_pass);
    if (res != VK_SUCCESS)
        return -1;

    return 0;
}

static VkImageAspectFlags get_vk_image_aspect_flags(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_D32_SFLOAT:
        return VK_IMAGE_ASPECT_DEPTH_BIT;
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default:
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

int ngli_rendertarget_init(struct rendertarget *s, struct ngl_ctx *ctx, const struct rendertarget_params *params)
{
    struct vkcontext *vk = ctx->vkcontext;

    s->ctx = ctx;
    s->width = params->width;
    s->height = params->height;
    s->params = *params;

    ngli_assert(params->nb_colors <= NGLI_MAX_COLOR_ATTACHMENTS);

    struct rendertarget_desc desc = {0};
    for (int i = 0; i < params->nb_colors; i++) {
        const struct texture *color = params->colors[i];
        const struct texture_params *params = &color->params;
        desc.color_formats[desc.nb_color_formats++] = params->format;
    }
    if (params->depth_stencil) {
        const struct texture *depth_stencil = params->depth_stencil;
        const struct texture_params *params = &depth_stencil->params;
        desc.depth_stencil_format = params->format;
    }
    desc.samples = 0;

    int ret = ngli_vk_create_renderpass_info(ctx, &desc, &s->render_pass, 0);
    if (ret < 0)
        return ret;

    ret = ngli_vk_create_renderpass_info(ctx, &desc, &s->conservative_render_pass, 1);
    if (ret < 0)
        return ret;

    s->render_area = (VkExtent2D){s->width, s->height};

    for (int i = 0; i < params->nb_colors; i++) {
        const struct texture *texture = params->colors[i];

        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = texture->image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = texture->format,
            .subresourceRange.aspectMask = get_vk_image_aspect_flags(texture->format),
            .subresourceRange.baseMipLevel = 0,
            .subresourceRange.levelCount = 1,
            .subresourceRange.baseArrayLayer = 0,
            .subresourceRange.layerCount = 1,
        };

        VkResult vkret = vkCreateImageView(vk->device, &view_info, NULL, &s->attachments[s->nb_attachments]);
        if (vkret != VK_SUCCESS)
            return -1;
        s->nb_attachments++;
    }

    if (params->depth_stencil) {
        const struct texture *depth_stencil = params->depth_stencil;

        VkImageViewCreateInfo view_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = depth_stencil->image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = depth_stencil->format,
            .subresourceRange.aspectMask = get_vk_image_aspect_flags(depth_stencil->format),
            .subresourceRange.baseMipLevel = 0,
            .subresourceRange.levelCount = 1,
            .subresourceRange.baseArrayLayer = 0,
            .subresourceRange.layerCount = 1,
        };

        VkResult vkret = vkCreateImageView(vk->device, &view_info, NULL, &s->attachments[s->nb_attachments]);
        if (vkret != VK_SUCCESS)
            return -1;
        s->nb_attachments++;
    }

    VkFramebufferCreateInfo framebuffer_create_info = {
        .sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass      = s->render_pass,
        .attachmentCount = s->nb_attachments,
        .pAttachments    = s->attachments,
        .width           = s->width,
        .height          = s->height,
        .layers          = 1
    };

    VkResult res = vkCreateFramebuffer(vk->device, &framebuffer_create_info, NULL, &s->framebuffer);
    if (res != VK_SUCCESS)
        return -1;

    if (params->readable) {
        struct texture_params texture_params = NGLI_TEXTURE_PARAM_DEFAULTS;
        texture_params.width = s->width;
        texture_params.height = s->height;
        texture_params.format = NGLI_FORMAT_R8G8B8A8_UNORM;
        texture_params.staging = 1;

        int ret = ngli_texture_init(&s->staging_texture, ctx, &texture_params);
        if (ret < 0)
            return ret;
    }

    return 0;
}

void ngli_rendertarget_blit(struct rendertarget *s, struct rendertarget *dst, int vflip)
{
    LOG(ERROR, "stub");
}

void ngli_rendertarget_read_pixels(struct rendertarget *s, uint8_t *data)
{
    struct ngl_ctx *ctx = s->ctx;
    struct vkcontext *vk = ctx->vkcontext;
    struct rendertarget_params *params = &s->params;

    if (!params->readable) {
        LOG(ERROR, "rendertarget is not readable");
        return;
    }

    ngli_gctx_vk_end_render_pass(ctx);

    struct texture *src = params->colors[0];
    int ret = ngli_texture_vk_transition_layout(src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    if (ret < 0)
        return;

    struct texture *dst = &s->staging_texture;
    ret = ngli_texture_vk_transition_layout(dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (ret < 0)
        return;

    VkCommandBuffer command_buffer = vk->cur_command_buffer;

    const VkImageCopy imageCopyRegion = {
        .srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .srcSubresource.layerCount = 1,
        .dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .dstSubresource.layerCount = 1,
        .extent.width = s->width,
        .extent.height = s->height,
        .extent.depth = 1,
    };

    vkCmdCopyImage(command_buffer, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &imageCopyRegion);

    ret = ngli_texture_vk_transition_layout(dst, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    if (ret < 0)
        return;

    VkResult vkret = vkEndCommandBuffer(command_buffer);
    if (vkret != VK_SUCCESS)
        return;
    vk->cur_command_buffer_state = 0;

    VkSubmitInfo submit_info = {
        .sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount   = ngli_darray_count(&vk->wait_semaphores),
        .pWaitSemaphores      = ngli_darray_data(&vk->wait_semaphores),
        .pWaitDstStageMask    = ngli_darray_data(&vk->wait_stages),
        .commandBufferCount   = 1,
        .pCommandBuffers      = &command_buffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores    = NULL,
    };
    vk->wait_semaphores.count = 0;
    vk->wait_stages.count = 0;

    vkret = vkQueueSubmit(vk->graphic_queue, 1, &submit_info, vk->fences[vk->current_frame]);
    if (vkret != VK_SUCCESS) {
        return;
    }

    vkret = vkWaitForFences(vk->device, 1, &vk->fences[vk->current_frame], VK_TRUE, UINT64_MAX);
    if (vkret != VK_SUCCESS)
        return;

    vkret = vkResetFences(vk->device, 1, &vk->fences[vk->current_frame]);
    if (vkret != VK_SUCCESS)
        return;

    uint8_t *data_src = NULL;
    vkret = vkMapMemory(vk->device, dst->image_memory, 0, VK_WHOLE_SIZE, 0, (void**)&data_src);
    if (vkret != VK_SUCCESS)
        return;
    memcpy(data, data_src, s->width * s->height * 4);
    vkUnmapMemory(vk->device, dst->image_memory);

    VkCommandBufferBeginInfo command_buffer_begin_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT,
    };
    vkret = vkBeginCommandBuffer(vk->cur_command_buffer, &command_buffer_begin_info);
    if (vkret != VK_SUCCESS)
        return;;
    vk->cur_command_buffer_state = 1;
}

void ngli_rendertarget_reset(struct rendertarget *s)
{
    if (!s->ctx)
        return;

    /* FIXME: use fence / queue resource for destruction later on*/
    s->ctx->backend->wait_idle(s->ctx);

    struct vkcontext *vk = s->ctx->vkcontext;
    vkDestroyRenderPass(vk->device, s->render_pass, NULL);
    vkDestroyRenderPass(vk->device, s->conservative_render_pass, NULL);
    vkDestroyFramebuffer(vk->device, s->framebuffer, NULL);

    for (int i = 0; i < s->nb_attachments; i++)
        vkDestroyImageView(vk->device, s->attachments[i], NULL);

    ngli_texture_reset(&s->staging_texture);

    memset(s, 0, sizeof(*s));
}

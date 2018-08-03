/*
 * Copyright 2019 GoPro Inc.
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <string.h>

#include "gctx.h"
#include "gctx_vk.h"
#include "log.h"
#include "texture_vk.h"


void ngli_gctx_vk_commit_render_pass(struct ngl_ctx *s)
{
    struct vkcontext *vk = s->vkcontext;

    if (vk->cur_render_pass_state == 1)
        return;

    if (s->rendertarget) {
        struct rendertarget_params *params = &s->rendertarget->params;
        for (int i = 0; i < params->nb_colors; i++) {
            ngli_texture_vk_transition_layout(params->colors[i], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }

        if (params->depth_stencil)
            ngli_texture_vk_transition_layout(params->depth_stencil, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }

    VkCommandBuffer cmd_buf = vk->cur_command_buffer;
    VkRenderPassBeginInfo render_pass_begin_info = {
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = vk->cur_render_pass,
        .framebuffer = vk->cur_framebuffer,
        .renderArea = {
            .extent = vk->cur_render_area,
        },
        .clearValueCount = 0,
        .pClearValues = NULL,
    };
    vkCmdBeginRenderPass(cmd_buf, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
    vk->cur_render_pass_state = 1;
}

void ngli_gctx_vk_end_render_pass(struct ngl_ctx *s)
{
    struct vkcontext *vk = s->vkcontext;

    if (vk->cur_render_pass_state != 1)
        return;

    VkCommandBuffer cmd_buf = vk->cur_command_buffer;
    vkCmdEndRenderPass(cmd_buf);
    vk->cur_render_pass_state = 0;
}

void ngli_gctx_set_rendertarget(struct ngl_ctx *s, struct rendertarget *rt, int conservative)
{
    struct vkcontext *vk = s->vkcontext;
    if (vk->cur_render_pass && rt != s->rendertarget) {
        VkCommandBuffer cmd_buf = vk->cur_command_buffer;
        if (vk->cur_render_pass_state == 1)
            vkCmdEndRenderPass(cmd_buf);
    }

    s->rendertarget = rt;
    if (rt) {
        vk->cur_framebuffer = rt->framebuffer;
        vk->cur_render_pass = conservative ? rt->conservative_render_pass : rt->render_pass;
        vk->cur_render_area = rt->render_area;
    } else {
        struct vkcontext *vk = s->vkcontext;
        vk->cur_framebuffer = vk->framebuffers[vk->img_index];
        vk->cur_render_pass = conservative ? vk->conservative_render_pass : vk->render_pass;
        vk->cur_render_area = vk->extent;
    }
    vk->cur_render_pass_state = 0;
}

struct rendertarget *ngli_gctx_get_rendertarget(struct ngl_ctx *s)
{
    return s->rendertarget;
}

void ngli_gctx_set_viewport(struct ngl_ctx *s, const int *viewport)
{
    memcpy(s->viewport, viewport, sizeof(s->viewport));
}

void ngli_gctx_get_viewport(struct ngl_ctx *s, int *viewport)
{
    memcpy(viewport, &s->viewport, sizeof(s->viewport));
}

void ngli_gctx_set_scissor(struct ngl_ctx *s, const int *scissor)
{
    memcpy(&s->scissor, scissor, sizeof(s->scissor));
}

void ngli_gctx_get_scissor(struct ngl_ctx *s, int *scissor)
{
    memcpy(scissor, &s->scissor, sizeof(s->scissor));
}

void ngli_gctx_set_clear_color(struct ngl_ctx *s, const float *color)
{
    memcpy(s->clear_color, color, sizeof(s->clear_color));
}

void ngli_gctx_get_clear_color(struct ngl_ctx *s, float *color)
{
    memcpy(color, &s->clear_color, sizeof(s->clear_color));
}

void ngli_gctx_clear_color(struct ngl_ctx *s)
{
    ngli_gctx_vk_commit_render_pass(s);

    struct vkcontext *vk = s->vkcontext;

    VkClearAttachment clear_attachments = {
        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
        .colorAttachment = 0,
        .clearValue = {
            .color={.float32={s->clear_color[0], s->clear_color[1], s->clear_color[2], s->clear_color[3]}},
        },
    };

    VkClearRect clear_rect = {
        .rect = {
            .offset = {0},
            .extent = vk->cur_render_area,
        },
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    VkCommandBuffer cmd_buf = vk->cur_command_buffer;
    vkCmdClearAttachments(cmd_buf, 1, &clear_attachments, 1, &clear_rect);
}

void ngli_gctx_clear_depth_stencil(struct ngl_ctx *s)
{
    if (s->rendertarget) {
        struct rendertarget_params *params = &s->rendertarget->params;
        if (!params->depth_stencil)
            return;
    }

    ngli_gctx_vk_commit_render_pass(s);

    // FIXME: no-oop if there is not depth stencil

    struct vkcontext *vk = s->vkcontext;

    VkClearAttachment clear_attachments = {
        .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
        .clearValue = {
            .depthStencil = {
                .depth = 1.0f,
                .stencil = 0,
            },
        },
    };

    VkClearRect clear_rect = {
        .rect = {
            .offset = {0},
            .extent = vk->cur_render_area,
        },
        .baseArrayLayer = 0,
        .layerCount = 1,
    };

    VkCommandBuffer cmd_buf = vk->cur_command_buffer;
    vkCmdClearAttachments(cmd_buf, 1, &clear_attachments, 1, &clear_rect);
}

void ngli_gctx_invalidate_depth_stencil(struct ngl_ctx *s)
{
    /* TODO: it needs change of API to implement this feature on Vulkan */
}

void ngli_gctx_flush(struct ngl_ctx *s)
{
    struct vkcontext *vk = s->vkcontext;

    VkCommandBuffer cmd_buf = vk->cur_command_buffer;
    if (vk->cur_render_pass && vk->cur_render_pass_state == 1) {
        vkCmdEndRenderPass(cmd_buf);
        vk->cur_render_pass_state = 0;
    }

        VkResult vkret = vkEndCommandBuffer(cmd_buf);
        if (vkret != VK_SUCCESS)
            return;
        vk->cur_command_buffer_state = 0;

    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = ngli_darray_count(&vk->wait_semaphores),
        .pWaitSemaphores = ngli_darray_data(&vk->wait_semaphores),
        .pWaitDstStageMask = ngli_darray_data(&vk->wait_stages),
        .commandBufferCount = 1,
        .pCommandBuffers = &cmd_buf,
        .signalSemaphoreCount = ngli_darray_count(&vk->signal_semaphores),
        .pSignalSemaphores = ngli_darray_data(&vk->signal_semaphores),
    };

    VkResult ret = vkQueueSubmit(vk->graphic_queue, 1, &submit_info, vk->fences[vk->current_frame]);
    if (ret != VK_SUCCESS) {
        return;
    }

    vk->wait_semaphores.count = 0;
    vk->wait_stages.count = 0;
    //vk->command_buffers.count = 0;
}

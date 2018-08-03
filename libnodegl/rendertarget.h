/*
 * Copyright 2018 GoPro Inc.
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

#ifndef RENDERTARGET_H
#define RENDERTARGET_H

#include "darray.h"
#include "texture.h"

#define NGLI_MAX_COLOR_ATTACHMENTS 8

struct rendertarget_desc {
    int color_formats[NGLI_MAX_COLOR_ATTACHMENTS];
    int nb_color_formats;
    int depth_stencil_format;
    int samples;
};

struct rendertarget_params {
    int width;
    int height;
    int nb_colors;
    struct texture *colors[NGLI_MAX_COLOR_ATTACHMENTS];
    struct texture *depth_stencil;
    int readable;
};

struct rendertarget {
    struct ngl_ctx *ctx;
    int width;
    int height;
    int nb_color_attachments;
    struct rendertarget_params params;

#ifdef VULKAN_BACKEND
    int nb_attachments;
    VkImageView attachments[NGLI_MAX_COLOR_ATTACHMENTS + 1];
    VkFramebuffer framebuffer;
    VkRenderPass render_pass;
    VkRenderPass conservative_render_pass;
    VkExtent2D render_area;
    struct texture staging_texture;
#else
    GLuint id;
    GLuint prev_id;
    GLenum *draw_buffers;
    int nb_draw_buffers;
    GLenum *blit_draw_buffers;
    void (*blit)(struct rendertarget *s, struct rendertarget *dst, int vflip);
#endif
};

int ngli_rendertarget_init(struct rendertarget *s, struct ngl_ctx *ctx, const struct rendertarget_params *params);
void ngli_rendertarget_blit(struct rendertarget *s, struct rendertarget *dst, int vflip);
void ngli_rendertarget_read_pixels(struct rendertarget *s, uint8_t *data);
void ngli_rendertarget_reset(struct rendertarget *s);

int ngli_vk_create_renderpass_info(struct ngl_ctx *ctx, const struct rendertarget_desc *desc, VkRenderPass *render_pass, int conservative);

#endif

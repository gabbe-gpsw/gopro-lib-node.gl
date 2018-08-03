/*
 * Copyright 2016 GoPro Inc.
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

#ifndef VKCONTEXT_H
#define VKCONTEXT_H

#include <stdlib.h>

#include <shaderc/shaderc.h>
#include <vulkan/vulkan.h>

#include "darray.h"
#include "nodegl.h"
#include "rendertarget.h"
#include "texture.h"

#define NGLI_FEATURE_VERTEX_ARRAY_OBJECT          (1 << 0)
#define NGLI_FEATURE_TEXTURE_3D                   (1 << 1)
#define NGLI_FEATURE_TEXTURE_STORAGE              (1 << 2)
#define NGLI_FEATURE_COMPUTE_SHADER               (1 << 3)
#define NGLI_FEATURE_PROGRAM_INTERFACE_QUERY      (1 << 4)
#define NGLI_FEATURE_SHADER_IMAGE_LOAD_STORE      (1 << 5)
#define NGLI_FEATURE_SHADER_STORAGE_BUFFER_OBJECT (1 << 6)
#define NGLI_FEATURE_FRAMEBUFFER_OBJECT           (1 << 7)
#define NGLI_FEATURE_INTERNALFORMAT_QUERY         (1 << 8)
#define NGLI_FEATURE_PACKED_DEPTH_STENCIL         (1 << 9)
#define NGLI_FEATURE_TIMER_QUERY                  (1 << 10)
#define NGLI_FEATURE_EXT_DISJOINT_TIMER_QUERY     (1 << 11)
#define NGLI_FEATURE_DRAW_INSTANCED               (1 << 12)
#define NGLI_FEATURE_INSTANCED_ARRAY              (1 << 13)
#define NGLI_FEATURE_UNIFORM_BUFFER_OBJECT        (1 << 14)
#define NGLI_FEATURE_INVALIDATE_SUBDATA           (1 << 15)
#define NGLI_FEATURE_OES_EGL_EXTERNAL_IMAGE       (1 << 16)
#define NGLI_FEATURE_DEPTH_TEXTURE                (1 << 17)
#define NGLI_FEATURE_RGB8_RGBA8                   (1 << 18)
#define NGLI_FEATURE_OES_EGL_IMAGE                (1 << 19)
#define NGLI_FEATURE_EGL_IMAGE_BASE_KHR           (1 << 20)
#define NGLI_FEATURE_EGL_EXT_IMAGE_DMA_BUF_IMPORT (1 << 21)
#define NGLI_FEATURE_SYNC                         (1 << 22)
#define NGLI_FEATURE_YUV_TARGET                   (1 << 23)
#define NGLI_FEATURE_TEXTURE_NPOT                 (1 << 24)
#define NGLI_FEATURE_TEXTURE_CUBE_MAP             (1 << 25)
#define NGLI_FEATURE_DRAW_BUFFERS                 (1 << 26)
#define NGLI_FEATURE_ROW_LENGTH                   (1 << 27)
#define NGLI_FEATURE_SOFTWARE                     (1 << 28)

#define NGLI_FEATURE_COMPUTE_SHADER_ALL (NGLI_FEATURE_COMPUTE_SHADER           | \
                                         NGLI_FEATURE_PROGRAM_INTERFACE_QUERY  | \
                                         NGLI_FEATURE_SHADER_IMAGE_LOAD_STORE  | \
                                         NGLI_FEATURE_SHADER_STORAGE_BUFFER_OBJECT)

struct vk_swapchain_support {
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR *formats;
    uint32_t nb_formats;
    VkPresentModeKHR *present_modes;
    uint32_t nb_present_modes;
};

struct vkcontext {
    /* User options */
    struct ngl_config config;

    int platform;
    int backend;
    int offscreen;
    int width;
    int height;
    int samples;

    /* Vulkan api */
    int version;

    /* Vulkan features */
#if 0
    int features;
    int max_texture_image_units;
    int max_compute_work_group_counts[3];
    int max_uniform_block_size;
    int max_samples;
    int max_color_attachments;
    int max_draw_buffers;
#endif

    VkDevice device;
    VkExtent2D extent;
    VkRenderPass render_pass;
    VkRenderPass conservative_render_pass;

    VkQueue graphic_queue;
    VkQueue present_queue;

    VkInstance instance;
    VkDebugUtilsMessengerEXT report_callback;
    VkPhysicalDevice physical_device;
    VkPhysicalDeviceMemoryProperties phydev_mem_props;
    int queue_family_graphics_id;
    int queue_family_present_id;
    VkSurfaceKHR surface;
    struct vk_swapchain_support swapchain_support;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
    VkSwapchainKHR swapchain;
    VkImage *images;
    uint32_t nb_images;
    VkImageView *image_views;
    uint32_t nb_image_views;
    VkDeviceMemory *depth_device_memories;
    VkImage *depth_images;
    VkImageView *depth_image_views;
    uint32_t nb_depth_images;
    VkFramebuffer *framebuffers;
    int nb_framebuffers;
    VkSemaphore *sem_img_avail;
    VkSemaphore *sem_render_finished;
    VkFence *fences;
    VkStructureType surface_create_type;

    int prefered_depth_stencil_format;
    VkFormat prefered_vk_depth_stencil_format;

    uint32_t img_index;

    int nb_in_flight_frames;
    int current_frame;

    VkCommandPool transient_command_buffer_pool;
    VkFence transient_command_buffer_fence;

    VkCommandPool command_buffer_pool;
    VkCommandBuffer *command_buffers;
    VkCommandBuffer cur_command_buffer;
    int cur_command_buffer_state;

    shaderc_compiler_t spirv_compiler;
    shaderc_compile_options_t spirv_compiler_opts;

    struct texture rt_color;
    struct texture rt_depth_stencil;
    struct rendertarget rt;

    // Current framebuffer
    VkFramebuffer cur_framebuffer;
    VkRenderPass cur_render_pass;
    VkExtent2D cur_render_area;
    int cur_render_pass_state;
    struct darray wait_semaphores;
    struct darray wait_stages;
    struct darray signal_semaphores;

};

int ngli_vk_find_memory_type(struct vkcontext *vk, uint32_t type_filter, VkMemoryPropertyFlags props);
int ngli_vk_begin_transient_command(struct vkcontext *vk, VkCommandBuffer *command_buffer);
int ngli_vk_execute_transient_command(struct vkcontext *vk, VkCommandBuffer command_buffer);

#endif /* VKCONTEXT_H */

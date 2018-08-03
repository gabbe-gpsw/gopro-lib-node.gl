/*
 * Copyright 2016-2018 GoPro Inc.
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

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <vulkan/vulkan_core.h>

#include "darray.h"
#include "format.h"
#include "buffer.h"
#include "vkcontext.h"
#include "glincludes.h"
#include "hmap.h"
#include "image.h"
#include "log.h"
#include "math_utils.h"
#include "memory.h"
#include "nodegl.h"
#include "nodes.h"
#include "pipeline.h"
#include "type.h"
#include "texture.h"
#include "texture_vk.h"
#include "topology.h"
#include "spirv.h"
#include "utils.h"

struct uniform_pair {
    uint64_t offset;
    uint64_t index;
    uint64_t size;
    struct pipeline_uniform uniform;
};

struct attribute_pair {
    struct pipeline_attribute attribute;
};

struct buffer_pair {
    struct pipeline_buffer buffer;
};

struct texture_pair {
    struct pipeline_texture texture;
};

static int build_attribute_descs(struct pipeline *s, const struct pipeline_params *params)
{
    struct ngl_ctx *ctx = s->ctx;
    struct vkcontext *vk = ctx->vkcontext;

    ngli_darray_init(&s->vertex_attribute_descs, sizeof(VkVertexInputAttributeDescription), 0);
    ngli_darray_init(&s->vertex_binding_descs,   sizeof(VkVertexInputBindingDescription), 0);
    ngli_darray_init(&s->vertex_buffers, sizeof(VkBuffer), 0);
    ngli_darray_init(&s->vertex_offsets, sizeof(VkDeviceSize), 0);

    for (int i = 0; i < params->nb_attributes; i++) {
        const struct pipeline_attribute *attribute = &params->attributes[i];

        struct attribute_pair pair = {
            .attribute = *attribute,
        };
        if (!ngli_darray_push(&s->attribute_descs, &pair))
            return NGL_ERROR_MEMORY;

        VkVertexInputBindingDescription binding_desc = {
            .binding   = ngli_darray_count(&s->vertex_buffers),
            .stride    = attribute->stride,
            .inputRate = attribute->rate ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX,
        };
        if (!ngli_darray_push(&s->vertex_binding_descs, &binding_desc))
            return NGL_ERROR_MEMORY;

        VkFormat format = VK_FORMAT_UNDEFINED;
        int ret = ngli_format_get_vk_format(vk, attribute->format, &format);
        if (ret < 0)
            return ret;

        VkVertexInputAttributeDescription attr_desc = {
            .binding  = ngli_darray_count(&s->vertex_buffers),
            .location = attribute->location,
            .format   = format,
            .offset   = attribute->offset,
        };
        if (!ngli_darray_push(&s->vertex_attribute_descs, &attr_desc))
            return NGL_ERROR_MEMORY;

        struct buffer *buffer = attribute->buffer;
        if (!ngli_darray_push(&s->vertex_buffers, &buffer->vkbuf))
            return NGL_ERROR_MEMORY;

        VkDeviceSize offset = 0;
        if (!ngli_darray_push(&s->vertex_offsets, &offset))
            return NGL_ERROR_MEMORY;
    }

    return 0;
}

static const VkBlendFactor vk_blend_factor_map[NGLI_BLEND_FACTOR_NB] = {

    [NGLI_BLEND_FACTOR_ZERO]                = VK_BLEND_FACTOR_ZERO,
    [NGLI_BLEND_FACTOR_ONE]                 = VK_BLEND_FACTOR_ONE,
    [NGLI_BLEND_FACTOR_SRC_COLOR]           = VK_BLEND_FACTOR_SRC_COLOR,
    [NGLI_BLEND_FACTOR_ONE_MINUS_SRC_COLOR] = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    [NGLI_BLEND_FACTOR_DST_COLOR]           = VK_BLEND_FACTOR_DST_COLOR,
    [NGLI_BLEND_FACTOR_ONE_MINUS_DST_COLOR] = VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    [NGLI_BLEND_FACTOR_SRC_ALPHA]           = VK_BLEND_FACTOR_SRC_ALPHA,
    [NGLI_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA] = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    [NGLI_BLEND_FACTOR_DST_ALPHA]           = VK_BLEND_FACTOR_DST_ALPHA,
    [NGLI_BLEND_FACTOR_ONE_MINUS_DST_ALPHA] = VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA,
};

static const VkBlendFactor get_vk_blend_factor(int blend_factor)
{
    return vk_blend_factor_map[blend_factor];
}

static const VkBlendOp vk_blend_op_map[NGLI_BLEND_OP_NB] = {
    [NGLI_BLEND_OP_ADD]              = VK_BLEND_OP_ADD,
    [NGLI_BLEND_OP_SUBTRACT]         = VK_BLEND_OP_SUBTRACT,
    [NGLI_BLEND_OP_REVERSE_SUBTRACT] = VK_BLEND_OP_REVERSE_SUBTRACT,
    [NGLI_BLEND_OP_MIN]              = VK_BLEND_OP_MIN,
    [NGLI_BLEND_OP_MAX]              = VK_BLEND_OP_MAX,
};

static VkBlendOp get_vk_blend_op(int blend_op)
{
    return vk_blend_op_map[blend_op];
}

static const VkCompareOp vk_compare_op_map[NGLI_COMPARE_OP_NB] = {
    [NGLI_COMPARE_OP_NEVER]            = VK_COMPARE_OP_NEVER,
    [NGLI_COMPARE_OP_LESS]             = VK_COMPARE_OP_LESS,
    [NGLI_COMPARE_OP_EQUAL]            = VK_COMPARE_OP_EQUAL,
    [NGLI_COMPARE_OP_LESS_OR_EQUAL]    = VK_COMPARE_OP_LESS_OR_EQUAL,
    [NGLI_COMPARE_OP_GREATER]          = VK_COMPARE_OP_GREATER,
    [NGLI_COMPARE_OP_NOT_EQUAL]        = VK_COMPARE_OP_NOT_EQUAL,
    [NGLI_COMPARE_OP_GREATER_OR_EQUAL] = VK_COMPARE_OP_GREATER_OR_EQUAL,
    [NGLI_COMPARE_OP_ALWAYS]           = VK_COMPARE_OP_ALWAYS,
};

static VkCompareOp get_vk_compare_op(int compare_op)
{
    return vk_compare_op_map[compare_op];
}

static const VkStencilOp vk_stencil_op_map[NGLI_STENCIL_OP_NB] = {
    [NGLI_STENCIL_OP_KEEP]                = VK_STENCIL_OP_KEEP,
    [NGLI_STENCIL_OP_ZERO]                = VK_STENCIL_OP_ZERO,
    [NGLI_STENCIL_OP_REPLACE]             = VK_STENCIL_OP_REPLACE,
    [NGLI_STENCIL_OP_INCREMENT_AND_CLAMP] = VK_STENCIL_OP_INCREMENT_AND_CLAMP,
    [NGLI_STENCIL_OP_DECREMENT_AND_CLAMP] = VK_STENCIL_OP_DECREMENT_AND_CLAMP,
    [NGLI_STENCIL_OP_INVERT]              = VK_STENCIL_OP_INVERT,
    [NGLI_STENCIL_OP_INCREMENT_AND_WRAP]  = VK_STENCIL_OP_INCREMENT_AND_WRAP,
    [NGLI_STENCIL_OP_DECREMENT_AND_WRAP]  = VK_STENCIL_OP_DECREMENT_AND_WRAP,
};

static VkStencilOp get_vk_stencil_op(int stencil_op)
{
    return vk_stencil_op_map[stencil_op];
}

static const VkCullModeFlags vk_cull_mode_map[NGLI_CULL_MODE_NB] = {
    [NGLI_CULL_MODE_FRONT_BIT]      = VK_CULL_MODE_FRONT_BIT,
    [NGLI_CULL_MODE_BACK_BIT]       = VK_CULL_MODE_BACK_BIT,
    [NGLI_CULL_MODE_FRONT_AND_BACK] = VK_CULL_MODE_FRONT_AND_BACK,
};

static VkCullModeFlags get_vk_cull_mode(int cull_mode)
{
    return vk_cull_mode_map[cull_mode];
}

static VkColorComponentFlags get_vk_color_write_mask(int color_write_mask)
{
    return (color_write_mask & NGLI_COLOR_COMPONENT_R_BIT ? VK_COLOR_COMPONENT_R_BIT : 0)
         | (color_write_mask & NGLI_COLOR_COMPONENT_G_BIT ? VK_COLOR_COMPONENT_G_BIT : 0)
         | (color_write_mask & NGLI_COLOR_COMPONENT_B_BIT ? VK_COLOR_COMPONENT_B_BIT : 0)
         | (color_write_mask & NGLI_COLOR_COMPONENT_A_BIT ? VK_COLOR_COMPONENT_A_BIT : 0);
}

static int pipeline_graphics_init(struct pipeline *s, const struct pipeline_params *params)
{
    struct ngl_ctx *ctx = s->ctx;
    struct vkcontext *vk = ctx->vkcontext;
    struct pipeline_graphics *graphics = &s->graphics;
    struct graphicstate *state = &graphics->state;

    int ret = build_attribute_descs(s, params);
    if (ret < 0)
        return ret;

    VkPipelineVertexInputStateCreateInfo vertex_input_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = ngli_darray_count(&s->vertex_binding_descs),
        .pVertexBindingDescriptions = ngli_darray_data(&s->vertex_binding_descs),
        .vertexAttributeDescriptionCount = ngli_darray_count(&s->vertex_attribute_descs),
        .pVertexAttributeDescriptions = ngli_darray_data(&s->vertex_attribute_descs),
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = ngli_topology_get_vk_topology(graphics->topology),
    };

    /* Viewport */
    VkViewport viewport = {
        .width = vk->config.width,
        .height = vk->config.height,
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };

    VkRect2D scissor = {
        .extent = vk->extent,
    };

    VkPipelineViewportStateCreateInfo viewport_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    /* Rasterization */
    VkPipelineRasterizationStateCreateInfo rasterization_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.f,
        .cullMode = get_vk_cull_mode(state->cull_face_mode),
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
    };

    /* Multisampling */
    VkPipelineMultisampleStateCreateInfo multisampling_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };

    /* Depth & stencil */
    VkPipelineDepthStencilStateCreateInfo depthstencil_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = state->depth_test,
        .depthWriteEnable = state->depth_write_mask,
        .depthCompareOp = get_vk_compare_op(state->depth_func),
        .depthBoundsTestEnable = 0,
        .stencilTestEnable = state->stencil_test,
        .front = {
            .failOp = get_vk_stencil_op(state->stencil_fail),
            .passOp = get_vk_stencil_op(state->stencil_depth_pass),
            .depthFailOp = get_vk_stencil_op(state->stencil_depth_fail),
            .compareOp = get_vk_compare_op(state->stencil_func),
            .compareMask = state->stencil_read_mask,
            .writeMask = state->stencil_write_mask,
            .reference = state->stencil_ref,
        },
        .back = {
            .failOp = get_vk_stencil_op(state->stencil_fail),
            .passOp = get_vk_stencil_op(state->stencil_depth_pass),
            .depthFailOp = get_vk_stencil_op(state->stencil_depth_fail),
            .compareOp = get_vk_compare_op(state->stencil_func),
            .compareMask = state->stencil_read_mask,
            .writeMask = state->stencil_write_mask,
            .reference = state->stencil_ref,
        },
        .minDepthBounds = 0.0f,
        .maxDepthBounds = 0.0f,
    };

    /* Blend */
    VkPipelineColorBlendAttachmentState colorblend_attachment_state = {
        .blendEnable = state->blend,
        .srcColorBlendFactor = get_vk_blend_factor(state->blend_src_factor),
        .dstColorBlendFactor = get_vk_blend_factor(state->blend_dst_factor),
        .colorBlendOp = get_vk_blend_op(state->blend_op),
        .srcAlphaBlendFactor = get_vk_blend_factor(state->blend_src_factor_a),
        .dstAlphaBlendFactor = get_vk_blend_factor(state->blend_dst_factor_a),
        .alphaBlendOp = get_vk_blend_op(state->blend_op_a),
        .colorWriteMask = get_vk_color_write_mask(state->color_write_mask),
    };

    VkPipelineColorBlendStateCreateInfo colorblend_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &colorblend_attachment_state,
    };

    /* Dynamic states */
    VkDynamicState dynamic_states[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_LINE_WIDTH,
    };

    VkPipelineDynamicStateCreateInfo dynamic_state_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = NGLI_ARRAY_NB(dynamic_states),
        .pDynamicStates = dynamic_states,

    };

    const struct program *program = s->program;
    VkPipelineShaderStageCreateInfo shader_stage_create_info[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = program->shaders[NGLI_PROGRAM_SHADER_VERT].vkmodule,
            .pName = "main",
        },{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = program->shaders[NGLI_PROGRAM_SHADER_FRAG].vkmodule,
            .pName = "main",
        },
    };

    VkRenderPass render_pass;
    ret = ngli_vk_create_renderpass_info(ctx, &graphics->rt_desc, &render_pass, 0);
    if (ret < 0)
        return ret;

    VkGraphicsPipelineCreateInfo graphics_pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = NGLI_ARRAY_NB(shader_stage_create_info),
        .pStages = shader_stage_create_info,
        .pVertexInputState = &vertex_input_state_create_info,
        .pInputAssemblyState = &input_assembly_state_create_info,
        .pViewportState = &viewport_state_create_info,
        .pRasterizationState = &rasterization_state_create_info,
        .pMultisampleState = &multisampling_state_create_info,
        .pDepthStencilState = &depthstencil_state_create_info,
        .pColorBlendState = &colorblend_state_create_info,
        .pDynamicState = &dynamic_state_create_info,
        .layout = s->pipeline_layout,
        .renderPass = render_pass,
        .subpass = 0,
    };

    // FIXME: leak rp

    VkResult vkret = vkCreateGraphicsPipelines(vk->device, NULL, 1, &graphics_pipeline_create_info, NULL, &s->pipeline);
    if (vkret != VK_SUCCESS)
        return NGL_ERROR_EXTERNAL;

    vkDestroyRenderPass(vk->device, render_pass, NULL);

    s->bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;

    return 0;
}

static int pipeline_compute_init(struct pipeline *s)
{
    struct ngl_ctx *ctx = s->ctx;
    struct vkcontext *vk = ctx->vkcontext;

    const struct program *program = s->program;

    VkPipelineShaderStageCreateInfo shader_stage_create_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = program->shaders[NGLI_PROGRAM_SHADER_COMP].vkmodule,
        .pName = "main",
    };

    VkComputePipelineCreateInfo compute_pipeline_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = shader_stage_create_info,
        .layout = s->pipeline_layout,
    };

    VkResult vkret = vkCreateComputePipelines(vk->device, NULL, 1, &compute_pipeline_create_info, NULL, &s->pipeline);
    if (vkret != VK_SUCCESS)
        return NGL_ERROR_EXTERNAL;

    s->bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;

    return 0;
}

#if 0
static const VkShaderStageFlags stage_flag_map[] = {
    [NGLI_PROGRAM_SHADER_VERT] = VK_SHADER_STAGE_VERTEX_BIT,
    [NGLI_PROGRAM_SHADER_FRAG] = VK_SHADER_STAGE_FRAGMENT_BIT,
    [NGLI_PROGRAM_SHADER_COMP] = VK_SHADER_STAGE_COMPUTE_BIT,
};

// TODO: honor stage flags ?
static VkShaderStageFlags get_stage_flags(int stages)
{
    VkShaderStageFlags flags = 0;
    if (stages & (1 << NGLI_PROGRAM_SHADER_VERT))
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (stages & (1 << NGLI_PROGRAM_SHADER_FRAG))
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (stages & (1 << NGLI_PROGRAM_SHADER_COMP))
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    return flags;
}
#endif

static const VkDescriptorType descriptor_type_map[] = {
    [NGLI_TYPE_UNIFORM_BUFFER] = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    [NGLI_TYPE_STORAGE_BUFFER] = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
    [NGLI_TYPE_SAMPLER_2D]     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    [NGLI_TYPE_SAMPLER_3D]     = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
    [NGLI_TYPE_IMAGE_2D]       = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
};

static VkDescriptorType get_descriptor_type(int type)
{
    return descriptor_type_map[type];
}

static int create_desc_set_layout_bindings(struct pipeline *s, const struct pipeline_params *params)
{
    struct ngl_ctx *ctx = s->ctx;
    struct vkcontext *vk = ctx->vkcontext;

    ngli_darray_init(&s->desc_set_layout_bindings, sizeof(VkDescriptorSetLayoutBinding), 0);

    VkDescriptorPoolSize desc_pool_size_map[] = {
        [NGLI_TYPE_UNIFORM_BUFFER] = {.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
        [NGLI_TYPE_STORAGE_BUFFER] = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        [NGLI_TYPE_SAMPLER_2D]     = {.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
        [NGLI_TYPE_IMAGE_2D]       = {.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
    };

    for (int i = 0; i < params->nb_buffers; i++) {
        const struct pipeline_buffer *pipeline_buffer = &params->buffers[i];

        const VkDescriptorType type = get_descriptor_type(pipeline_buffer->type);
        const VkDescriptorSetLayoutBinding binding = {
            .binding         = pipeline_buffer->binding,
            .descriptorType  = type,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_ALL,
        };
        if (!ngli_darray_push(&s->desc_set_layout_bindings, &binding))
            return NGL_ERROR_MEMORY;

        struct buffer_pair pair = {
            .buffer = *pipeline_buffer,
        };
        if (!ngli_darray_push(&s->buffer_descs, &pair))
            return NGL_ERROR_MEMORY;

        desc_pool_size_map[pipeline_buffer->type].descriptorCount += vk->nb_framebuffers;
    }

    /* XXX */
    {
        struct hmap *blocks = s->program->buffer_blocks;
        struct program_variable_info *ngl_block = ngli_hmap_get(blocks, "ngl");
        if (ngl_block) {
            const VkDescriptorType type = get_descriptor_type(ngl_block->type);

            s->uniform_binding = ngl_block->binding;
            s->uniform_type = type;
            s->uniform_data = ngli_calloc(1, ngl_block->size);
            if (!s->uniform_data)
                return NGL_ERROR_MEMORY;

            const VkDescriptorSetLayoutBinding binding = {
                .binding         = s->uniform_binding,
                .descriptorType  = s->uniform_type,
                .descriptorCount = 1,
                .stageFlags      = VK_SHADER_STAGE_ALL,
            };
            if (!ngli_darray_push(&s->desc_set_layout_bindings, &binding))
                return NGL_ERROR_MEMORY;

            desc_pool_size_map[ngl_block->type].descriptorCount += vk->nb_framebuffers;

            int ret = ngli_buffer_init(&s->uniform_buffer, s->ctx, ngl_block->size, NGLI_BUFFER_USAGE_DYNAMIC);
            if (ret < 0)
                return ret;
        }
    }

    for (int i = 0; i < params->nb_textures; i++) {
        const struct pipeline_texture *pipeline_texture = &params->textures[i];

        const VkDescriptorType type = get_descriptor_type(pipeline_texture->type);
        const VkDescriptorSetLayoutBinding binding = {
            .binding         = pipeline_texture->binding,
            .descriptorType  = type,
            .descriptorCount = 1,
            .stageFlags      = VK_SHADER_STAGE_ALL,
        };
        if (!ngli_darray_push(&s->desc_set_layout_bindings, &binding))
            return NGL_ERROR_MEMORY;

        struct texture_pair pair = {
            .texture = *pipeline_texture,
        };
        if (!ngli_darray_push(&s->texture_descs, &pair))
            return NGL_ERROR_MEMORY;

        desc_pool_size_map[pipeline_texture->type].descriptorCount += vk->nb_framebuffers;
    }

    struct VkDescriptorPoolSize desc_pool_sizes[NGLI_ARRAY_NB(desc_pool_size_map)];
    int nb_desc_pool_sizes = 0;
    for (int i = 0; i < NGLI_ARRAY_NB(desc_pool_size_map); i++) {
        if (desc_pool_size_map[i].descriptorCount) {
            desc_pool_sizes[nb_desc_pool_sizes++] = desc_pool_size_map[i];
        }
    }

    if (!nb_desc_pool_sizes)
        return 0;

    VkDescriptorPoolCreateInfo descriptor_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .poolSizeCount = nb_desc_pool_sizes,
        .pPoolSizes = desc_pool_sizes,
        .maxSets = vk->nb_framebuffers,
    };

    VkResult vkret = vkCreateDescriptorPool(vk->device, &descriptor_pool_create_info, NULL, &s->desc_pool);
    if (vkret != VK_SUCCESS)
        return NGL_ERROR_EXTERNAL;

    VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = ngli_darray_count(&s->desc_set_layout_bindings),
        .pBindings = (const VkDescriptorSetLayoutBinding *)ngli_darray_data(&s->desc_set_layout_bindings),
    };

    vkret = vkCreateDescriptorSetLayout(vk->device, &descriptor_set_layout_create_info, NULL, &s->desc_set_layout);
    if (vkret != VK_SUCCESS)
        return NGL_ERROR_EXTERNAL;

    VkDescriptorSetLayout *desc_set_layouts = ngli_calloc(vk->nb_framebuffers, sizeof(*desc_set_layouts));
    if (!desc_set_layouts)
        return NGL_ERROR_MEMORY;

    for (int i = 0; i < vk->nb_framebuffers; i++)
        desc_set_layouts[i] = s->desc_set_layout;

    VkDescriptorSetAllocateInfo descriptor_set_allocate_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = s->desc_pool,
        .descriptorSetCount = vk->nb_framebuffers,
        .pSetLayouts = desc_set_layouts
    };

    s->desc_sets = ngli_calloc(vk->nb_framebuffers, sizeof(*s->desc_sets));
    if (!s->desc_sets) {
        ngli_free(desc_set_layouts);
        return NGL_ERROR_MEMORY;
    }

    vkret = vkAllocateDescriptorSets(vk->device, &descriptor_set_allocate_info, s->desc_sets);
    if (vkret != VK_SUCCESS) {
        ngli_free(desc_set_layouts);
        return NGL_ERROR_EXTERNAL;
    }

    for (int i = 0; i < params->nb_buffers; i++) {
        const struct pipeline_buffer *pipeline_buffer = &params->buffers[i];
        struct buffer *buffer = pipeline_buffer->buffer;

        for (int i = 0; i < vk->nb_framebuffers; i++) {
            const VkDescriptorBufferInfo descriptor_buffer_info = {
                .buffer = buffer->vkbuf,
                .offset = 0,
                .range  = buffer->size,
            };
            const VkDescriptorType type = get_descriptor_type(pipeline_buffer->type);
            const VkWriteDescriptorSet write_descriptor_set = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = s->desc_sets[i],
                .dstBinding = pipeline_buffer->binding,
                .dstArrayElement = 0,
                .descriptorType = type,
                .descriptorCount = 1,
                .pBufferInfo = &descriptor_buffer_info,
                .pImageInfo = NULL,
                .pTexelBufferView = NULL,
            };
            vkUpdateDescriptorSets(vk->device, 1, &write_descriptor_set, 0, NULL);
        }
    }

    for (int i = 0; i < params->nb_textures; i++) {
        const struct pipeline_texture *pipeline_texture = &params->textures[i];
        struct texture *texture = pipeline_texture->texture;
        if (!texture)
            continue;

        for (int i = 0; i < vk->nb_framebuffers; i++) {
            VkDescriptorImageInfo image_info = {
                .imageLayout = texture->image_layout,
                .imageView   = texture->image_view,
                .sampler     = texture->image_sampler,
            };
            VkWriteDescriptorSet write_descriptor_set = {
                .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet           = s->desc_sets[i],
                .dstBinding       = pipeline_texture->binding,
                .dstArrayElement  = 0,
                .descriptorType   = get_descriptor_type(pipeline_texture->type),
                .descriptorCount  = 1,
                .pImageInfo       = &image_info,
            };
            vkUpdateDescriptorSets(vk->device, 1, &write_descriptor_set, 0, NULL);
        }
    }

    {
        for (int i = 0; i < vk->nb_framebuffers; i++) {
            if (s->uniform_data) {
                const VkDescriptorBufferInfo descriptor_buffer_info = {
                    .buffer = s->uniform_buffer.vkbuf,
                    .offset = 0,
                    .range  = s->uniform_buffer.size,
                };
                const VkWriteDescriptorSet write_descriptor_set = {
                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                    .dstSet = s->desc_sets[i],
                    .dstBinding = s->uniform_binding,
                    .dstArrayElement = 0,
                    .descriptorType = s->uniform_type,
                    .descriptorCount = 1,
                    .pBufferInfo = &descriptor_buffer_info,
                    .pImageInfo = NULL,
                    .pTexelBufferView = NULL,
                };
                vkUpdateDescriptorSets(vk->device, 1, &write_descriptor_set, 0, NULL);
            }
        }
    }

    ngli_free(desc_set_layouts);
    return 0;
}

static int create_pipeline_layout(struct pipeline *s)
{
    struct ngl_ctx *ctx = s->ctx;
    struct vkcontext *vk = ctx->vkcontext;

    // FIXME: handle push constants
    VkPipelineLayoutCreateInfo pipeline_layout_create_info = {
        .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = s->desc_set_layout ? 1 : 0,
        .pSetLayouts    = &s->desc_set_layout,
    };

    VkResult vkret = vkCreatePipelineLayout(vk->device, &pipeline_layout_create_info, NULL, &s->pipeline_layout);
    if (vkret != VK_SUCCESS)
        return NGL_ERROR_EXTERNAL;

    return 0;
}

static const int type_size_map[NGLI_TYPE_NB] = {
    [NGLI_TYPE_INT]                         = sizeof(int),
    [NGLI_TYPE_IVEC2]                       = sizeof(int) * 2,
    [NGLI_TYPE_IVEC3]                       = sizeof(int) * 3,
    [NGLI_TYPE_IVEC4]                       = sizeof(int) * 4,
    [NGLI_TYPE_UINT]                        = sizeof(unsigned int),
    [NGLI_TYPE_UIVEC2]                      = sizeof(unsigned int) * 2,
    [NGLI_TYPE_UIVEC3]                      = sizeof(unsigned int) * 3,
    [NGLI_TYPE_UIVEC4]                      = sizeof(unsigned int) * 4,
    [NGLI_TYPE_FLOAT]                       = sizeof(float),
    [NGLI_TYPE_VEC2]                        = sizeof(float) * 2,
    [NGLI_TYPE_VEC3]                        = sizeof(float) * 3,
    [NGLI_TYPE_VEC4]                        = sizeof(float) * 4,
    [NGLI_TYPE_MAT3]                        = sizeof(float) * 3 * 3,
    [NGLI_TYPE_MAT4]                        = sizeof(float) * 4 * 4,
    [NGLI_TYPE_BOOL]                        = sizeof(int),
};

static int build_uniform_descs(struct pipeline *s, const struct pipeline_params *params)
{
    const struct program *program = params->program;

    if (!program->uniforms)
        return 0;

    for (int i = 0; i < params->nb_uniforms; i++) {
        const struct pipeline_uniform *uniform = &params->uniforms[i];
        const struct program_variable_info *info = ngli_hmap_get(program->uniforms, uniform->name);
        if (!info)
            continue;

        if (uniform->type != info->type && (uniform->type != NGLI_TYPE_INT ||
            (info->type != NGLI_TYPE_BOOL && info->type != NGLI_TYPE_INT))) {
            LOG(ERROR, "uniform '%s' type does not match the type declared in the shader", uniform->name);
            return NGL_ERROR_INVALID_ARG;
        }

        struct uniform_pair pair = {
            .offset = info->offset,
            .index = info->index,
            .size = type_size_map[info->type],
            .uniform = *uniform,
        };
        if (!ngli_darray_push(&s->uniform_descs, &pair))
            return NGL_ERROR_MEMORY;
    }

    return 0;
}

static int set_uniforms(struct pipeline *s)
{
    if (!s->uniform_data)
        return 0;

    struct uniform_pair *pairs = ngli_darray_data(&s->uniform_descs);
    for (int i = 0; i < ngli_darray_count(&s->uniform_descs); i++) {
        struct uniform_pair *pair = &pairs[i];
        struct pipeline_uniform *uniform = &pair->uniform;
        if (uniform->data)
            memcpy(s->uniform_data + pair->offset, uniform->data, pair->size);
    }

    ngli_buffer_upload(&s->uniform_buffer, s->uniform_data, s->uniform_buffer.size);

    return 0;
}

int ngli_pipeline_init(struct pipeline *s, struct ngl_ctx *ctx, const struct pipeline_params *params)
{
    int ret;

    memset(s, 0, sizeof(*s));

    s->ctx      = ctx;
    s->type     = params->type;
    s->graphics = params->graphics;
    s->compute  = params->compute;
    s->program  = params->program;

    ngli_darray_init(&s->uniform_descs, sizeof(struct uniform_pair), 0);
    ngli_darray_init(&s->texture_descs, sizeof(struct texture_pair), 0);
    ngli_darray_init(&s->buffer_descs,  sizeof(struct buffer_pair), 0);
    ngli_darray_init(&s->attribute_descs, sizeof(struct attribute_pair), 0);

    if ((ret = create_desc_set_layout_bindings(s, params)) < 0)
        return ret;

    ret = build_uniform_descs(s, params);
    if (ret < 0)
        return ret;

    if ((ret = create_pipeline_layout(s)) < 0)
        return ret;

    if (params->type == NGLI_PIPELINE_TYPE_GRAPHICS) {
        ret = pipeline_graphics_init(s, params);
        if (ret < 0)
            return ret;
    } else if (params->type == NGLI_PIPELINE_TYPE_COMPUTE) {
        ret = pipeline_compute_init(s);
        if (ret < 0)
            return ret;
    } else {
        ngli_assert(0);
    }

    return 0;
}

int ngli_pipeline_get_uniform_index(const struct pipeline *s, const char *name)
{
    struct uniform_pair *pairs = ngli_darray_data(&s->uniform_descs);
    for (int i = 0; i < ngli_darray_count(&s->uniform_descs); i++) {
        struct uniform_pair *pair = &pairs[i];
        struct pipeline_uniform *uniform = &pair->uniform;
        if (!strcmp(uniform->name, name))
            return i;
    }
    return -1;
}

int ngli_pipeline_get_texture_index(const struct pipeline *s, const char *name)
{
    struct pipeline_texture *pairs = ngli_darray_data(&s->texture_descs);
    for (int i = 0; i < ngli_darray_count(&s->texture_descs); i++) {
        struct pipeline_texture *pipeline_texture = &pairs[i];
        if (!strcmp(pipeline_texture->name, name))
            return i;
    }
    return -1;
}

int ngli_pipeline_update_uniform(struct pipeline *s, int index, const void *data)
{
    if (index < 0)
        return NGL_ERROR_NOT_FOUND;

    ngli_assert(index < ngli_darray_count(&s->uniform_descs));
    struct uniform_pair *pairs = ngli_darray_data(&s->uniform_descs);
    struct uniform_pair *pair = &pairs[index];
    struct pipeline_uniform *pipeline_uniform = &pair->uniform;
    pipeline_uniform->data = data;

    return 0;
}

int ngli_pipeline_update_texture(struct pipeline *s, int index, struct texture *texture)
{
    struct ngl_ctx *ctx = s->ctx;
    struct vkcontext *vk = ctx->vkcontext;

    ngli_assert(index < ngli_darray_count(&s->texture_descs));
    struct pipeline_texture *pairs = ngli_darray_data(&s->texture_descs);
    struct pipeline_texture *pipeline_texture = &pairs[index];
    pipeline_texture->texture = texture;
    ngli_texture_vk_transition_layout(texture, VK_IMAGE_LAYOUT_GENERAL);

    VkDescriptorImageInfo image_info = {
        .imageLayout = texture->image_layout,
        .imageView   = texture->image_view,
        .sampler     = texture->image_sampler,
    };
    VkWriteDescriptorSet write_descriptor_set = {
        .sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet           = s->desc_sets[vk->img_index],
        .dstBinding       = pipeline_texture->binding,
        .dstArrayElement  = 0,
        .descriptorType   = get_descriptor_type(pipeline_texture->type),
        .descriptorCount  = 1,
        .pImageInfo       = &image_info,
    };
    vkUpdateDescriptorSets(vk->device, 1, &write_descriptor_set, 0, NULL);

    return 0;
}

static const VkIndexType vk_indices_type_map[NGLI_FORMAT_NB] = {
    [NGLI_FORMAT_R16_UNORM] = VK_INDEX_TYPE_UINT16,
    [NGLI_FORMAT_R32_UINT]  = VK_INDEX_TYPE_UINT32,
};

static VkIndexType get_vk_indices_type(int indices_format)
{
    return vk_indices_type_map[indices_format];
}

#include "gctx_vk.h"
#include "texture_vk.h"

void ngli_pipeline_exec(struct pipeline *s)
{
    struct ngl_ctx *ctx = s->ctx;
    struct vkcontext *vk = ctx->vkcontext;

    set_uniforms(s);

    VkCommandBuffer cmd_buf = vk->cur_command_buffer;

    ngli_gctx_vk_commit_render_pass(ctx);

    vkCmdBindPipeline(cmd_buf, s->bind_point, s->pipeline);


    struct texture_pair *pairs = ngli_darray_data(&s->texture_descs);
    for (int i = 0; i < ngli_darray_count(&s->texture_descs); i++) {
        struct pipeline_texture *texture = &pairs[i].texture;
        ngli_texture_vk_transition_layout(texture->texture, VK_IMAGE_LAYOUT_GENERAL);
    }

    if (s->desc_sets)
        vkCmdBindDescriptorSets(cmd_buf, s->bind_point, s->pipeline_layout, 0, 1, &s->desc_sets[vk->img_index], 0, NULL);

    if (s->type == NGLI_PIPELINE_TYPE_GRAPHICS) {
        struct pipeline_graphics *graphics = &s->graphics;

        VkViewport viewport = {
            .x = ctx->viewport[0],
            .y = ctx->viewport[1],
            .width = ctx->viewport[2],
            .height = ctx->viewport[3],
            .minDepth = 0.f,
            .maxDepth = 1.f,
        };
        vkCmdSetViewport(cmd_buf, 0, 1, &viewport);


        VkRect2D scissor;
        if (graphics->state.scissor_test) {
            scissor.offset.x = ctx->scissor[0];
            scissor.offset.y = ctx->scissor[1];
            scissor.extent.width = ctx->scissor[2];
            scissor.extent.height = ctx->scissor[3];
        } else {
            scissor.offset.x = 0;
            scissor.offset.y = 0;
            scissor.extent = ctx->rendertarget ? vk->cur_render_area : vk->extent;
        }

        vkCmdSetScissor(cmd_buf, 0, 1, &scissor);

        const int nb_vertex_buffers = ngli_darray_count(&s->vertex_buffers);
        VkBuffer *vertex_buffers = ngli_darray_data(&s->vertex_buffers);
        VkDeviceSize *vertex_offsets = ngli_darray_data(&s->vertex_offsets);
        vkCmdBindVertexBuffers(cmd_buf, 0, nb_vertex_buffers, vertex_buffers, vertex_offsets);

        struct buffer *indices = graphics->indices;
        int nb_instances = graphics->nb_instances ? graphics->nb_instances : 1;
        if (indices) {
            VkIndexType indices_type = get_vk_indices_type(graphics->indices_format);
            vkCmdBindIndexBuffer(cmd_buf, indices->vkbuf, 0, indices_type);
            vkCmdDrawIndexed(cmd_buf, graphics->nb_indices, nb_instances, 0, 0, 0);
        } else {
            vkCmdDraw(cmd_buf, graphics->nb_vertices, nb_instances, 0, 0);
        }
    } else {
        struct pipeline_compute *compute = &s->compute;
        ngli_gctx_vk_end_render_pass(ctx);
        vkCmdDispatch(cmd_buf, compute->nb_group_x, compute->nb_group_y, compute->nb_group_z);
    }
}

void ngli_pipeline_reset(struct pipeline *s)
{
    if (!s->ctx)
        return;

    struct ngl_ctx *ctx = s->ctx;
    struct vkcontext *vk = ctx->vkcontext;

    vkDeviceWaitIdle(vk->device);

    vkDestroyDescriptorPool(vk->device, s->desc_pool, NULL);
    vkDestroyDescriptorSetLayout(vk->device, s->desc_set_layout, NULL);
    ngli_free(s->desc_sets);

    vkDestroyPipeline(vk->device, s->pipeline, NULL);
    vkDestroyPipelineLayout(vk->device, s->pipeline_layout, NULL);

    ngli_buffer_reset(&s->uniform_buffer);

    ngli_darray_reset(&s->vertex_attribute_descs);
    ngli_darray_reset(&s->vertex_binding_descs);
    ngli_darray_reset(&s->vertex_buffers);
    ngli_darray_reset(&s->vertex_offsets);
    ngli_darray_reset(&s->desc_set_layout_bindings);

    memset(s, 0, sizeof(*s));
}

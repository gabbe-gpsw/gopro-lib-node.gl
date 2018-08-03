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

#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "memory.h"
#include "nodes.h"
#include "program.h"
#include "program_reflection.h"

int ngli_program_init(struct program *s, struct ngl_ctx *ctx, const char *vertex, const char *fragment, const char *compute)
{
    struct {
        shaderc_shader_kind kind;
        const char *src;
    } shaders[] = {
        [NGLI_PROGRAM_SHADER_VERT] = {shaderc_glsl_vertex_shader,   vertex,},
        [NGLI_PROGRAM_SHADER_FRAG] = {shaderc_glsl_fragment_shader, fragment},
        [NGLI_PROGRAM_SHADER_COMP] = {shaderc_glsl_compute_shader,  compute},
    };

    struct vkcontext *vk = ctx->vkcontext;

    s->ctx = ctx;

    struct program_reflection r = {0};
    int ret = ngli_program_reflection_init(&r, vertex, fragment, compute);
    if (ret < 0)
        return ret;
    ngli_program_reflection_dump(&r);
    s->uniforms = r.uniforms;
    r.uniforms = NULL;
    s->attributes = r.inputs;
    r.inputs = NULL;
    s->buffer_blocks = r.blocks;
    r.blocks = NULL;
    ngli_program_reflection_reset(&r);

    for (int i = 0; i < NGLI_ARRAY_NB(s->shaders); i++) {
        if (!shaders[i].src)
            continue;
        struct program_shader *shader = &s->shaders[i];

        shader->result = shaderc_compile_into_spv(vk->spirv_compiler,
                                                  shaders[i].src, strlen(shaders[i].src),
                                                  shaders[i].kind,
                                                  "whatever", "main", vk->spirv_compiler_opts);
        if (shaderc_result_get_compilation_status(shader->result) != shaderc_compilation_status_success)
            return -1;

        const uint32_t *code = (const uint32_t *)shaderc_result_get_bytes(shader->result);
        const size_t code_size = shaderc_result_get_length(shader->result);
        VkShaderModuleCreateInfo shader_module_create_info = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = code_size,
            .pCode = code,
        };
        VkResult ret = vkCreateShaderModule(vk->device, &shader_module_create_info, NULL, &shader->vkmodule);
        if (ret != VK_SUCCESS)
            return -1;
    }

    return 0;
}

void ngli_program_reset(struct program *s)
{
    if (!s->ctx)
        return;
    struct vkcontext *vk = s->ctx->vkcontext;
    for (int i = 0; i < NGLI_ARRAY_NB(s->shaders); i++) {
        struct program_shader *shader = &s->shaders[i];
        vkDestroyShaderModule(vk->device, shader->vkmodule, NULL);
        shaderc_result_release(shader->result);
    }
    memset(s, 0, sizeof(*s));
}

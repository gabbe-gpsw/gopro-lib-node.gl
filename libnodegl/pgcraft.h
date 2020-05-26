/*
 * Copyright 2020 GoPro Inc.
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

#ifndef PGCRAFT_H
#define PGCRAFT_H

#include "block.h"
#include "bstr.h"
#include "buffer.h"
#include "image.h"
#include "pipeline.h"
#include "texture.h"

struct ngl_ctx;

struct pgcraft_named_uniform { // also buffers (for arrays)
    char name[MAX_ID_LEN];
    int type;
    int stage;
    int count;
    int precision;
    const void *data;
};

enum pgcraft_shader_tex_type {
    NGLI_PGCRAFT_SHADER_TEX_TYPE_TEXTURE2D,
    NGLI_PGCRAFT_SHADER_TEX_TYPE_IMAGE2D,
    NGLI_PGCRAFT_SHADER_TEX_TYPE_TEXTURE3D,
    NGLI_PGCRAFT_SHADER_TEX_TYPE_CUBE,
    NB_NGLI_TEX_TYPE
};

struct pgcraft_named_texture {
    char name[MAX_ID_LEN];
    enum pgcraft_shader_tex_type type;
    int stage;
    int precision;
    int writable;
    int format;
    struct texture *texture;
    struct image *image;
};

struct pgcraft_named_block {
    char name[MAX_ID_LEN];
    int stage;
    int variadic;
    const struct block *block;
    struct buffer *buffer;
};

struct pgcraft_named_attribute {
    char name[MAX_ID_LEN];
    int type;
    int precision;
    int format;
    int stride;
    int offset;
    int rate;
    struct buffer *buffer;
};

enum {
    NGLI_INFO_FIELD_SAMPLING_MODE,
    NGLI_INFO_FIELD_DEFAULT_SAMPLER,
    NGLI_INFO_FIELD_COORDINATE_MATRIX,
    NGLI_INFO_FIELD_COLOR_MATRIX,
    NGLI_INFO_FIELD_DIMENSIONS,
    NGLI_INFO_FIELD_TIMESTAMP,
    NGLI_INFO_FIELD_OES_SAMPLER,
    NGLI_INFO_FIELD_Y_SAMPLER,
    NGLI_INFO_FIELD_UV_SAMPLER,
    NGLI_INFO_FIELD_Y_RECT_SAMPLER,
    NGLI_INFO_FIELD_UV_RECT_SAMPLER,
    NGLI_INFO_FIELD_NB
};

struct pgcraft_texture_info_field {
    char name[MAX_ID_LEN];
    int type;
    int index; // XXX: location
    int stage;
};

struct pgcraft_texture_info {
    int stage;
    int precision;
    int writable;
    int format;
    struct texture *texture;
    struct image *image;
    struct pgcraft_texture_info_field fields[NGLI_INFO_FIELD_NB];
};

struct pgcraft_params {
    const char *vert_base;
    const char *frag_base;
    const char *comp_base;

    const struct pgcraft_named_uniform *uniforms;
    int nb_uniforms;
    const struct pgcraft_named_texture *textures;
    int nb_textures;
    const struct pgcraft_named_block *blocks;
    int nb_blocks;
    const struct pgcraft_named_attribute *attributes;
    int nb_attributes;

    int nb_frag_output;
};


enum {
    BINDING_TYPE_UBO,
    BINDING_TYPE_SSBO,
    BINDING_TYPE_TEXTURE,
    NB_BINDING_TYPE
};

#define NB_BINDINGS (NGLI_PROGRAM_SHADER_NB * NB_BINDING_TYPE)
#define BIND_ID(stage, type) ((stage) * NB_BINDING_TYPE + (type))

struct pgcraft {
    struct darray texture_infos; // pgcraft_texture_info

    /* private */
    struct ngl_ctx *ctx;
    struct bstr *shaders[NGLI_PROGRAM_SHADER_NB];

    struct darray pipeline_uniforms;
    struct darray pipeline_textures;
    struct darray pipeline_buffers;
    struct darray pipeline_attributes;

    struct darray filtered_pipeline_uniforms;
    struct darray filtered_pipeline_textures;
    struct darray filtered_pipeline_buffers;
    struct darray filtered_pipeline_attributes;

    struct program program;

    int bindings[NB_BINDINGS];
    int *next_bindings[NB_BINDINGS];

    /* GLSL info */
    int glsl_version;
    const char *glsl_version_suffix;
    const char *rg; // red-green could be luma alpha
    int has_in_out_qualifiers;
    int has_precision_qualifiers;
    int has_modern_texture_picking;
    int has_buffer_bindings;
    int has_shared_bindings; // bindings shared across stages and types
};

int ngli_pgcraft_init(struct pgcraft *s, struct ngl_ctx *ctx);

int ngli_pgcraft_craft(struct pgcraft *s,
                       struct pipeline_params *dst,
                       const struct pgcraft_params *params);

void ngli_pgcraft_reset(struct pgcraft *s);

#endif

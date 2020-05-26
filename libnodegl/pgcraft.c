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

#include <string.h>
#include <stddef.h>

#include "format.h"
#include "hmap.h"
#include "log.h"
#include "nodes.h"
#include "pgcraft.h"
#include "type.h"

static const char *precision_qualifiers[] = {
    [NGLI_PRECISION_AUTO]   = NULL,
    [NGLI_PRECISION_HIGH]   = "highp",
    [NGLI_PRECISION_MEDIUM] = "mediump",
    [NGLI_PRECISION_LOW]    = "lowp",
};

static const char *get_precision_qualifier(const struct pgcraft *s, int precision)
{
    return s->has_precision_qualifiers ? precision_qualifiers[precision] : "";
}

static int inject_block_uniform(struct pgcraft *s, struct bstr *b, int stage,
                                const struct pgcraft_named_uniform *uniform)
{
    struct block *block = &s->ublock[stage];

    /* Lazily initialize the block containing the uniforms */
    if (!block->size)
        ngli_block_init(block, NGLI_BLOCK_LAYOUT_STD140);

    return ngli_block_add_field(block, uniform->name, uniform->type, uniform->count);
}

static int inject_uniform(struct pgcraft *s, struct bstr *b, int stage,
                          const struct pgcraft_named_uniform *uniform)
{
    if (uniform->stage != stage)
        return 0;

    if (s->use_ublock)
        return inject_block_uniform(s, b, stage, uniform);

    struct pipeline_uniform pl_uniform = {
        .type  = uniform->type,
        .count = NGLI_MAX(uniform->count, 1),
        .data  = uniform->data,
    };
    snprintf(pl_uniform.name, sizeof(pl_uniform.name), "%s", uniform->name);

    const char *type = ngli_type_get_glsl_type(uniform->type);
    const char *precision = get_precision_qualifier(s, uniform->precision);
    if (!precision)
        precision = "highp";
    if (uniform->count)
        ngli_bstr_printf(b, "uniform %s %s %s[%d];\n", precision, type, uniform->name, uniform->count);
    else
        ngli_bstr_printf(b, "uniform %s %s %s;\n", precision, type, uniform->name);

    if (!ngli_darray_push(&s->pipeline_uniforms, &pl_uniform))
        return NGL_ERROR_MEMORY;
    return 0;
}

static const char * const texture_info_suffixes[] = {
    [NGLI_INFO_FIELD_SAMPLING_MODE]     = "sampling_mode",
    [NGLI_INFO_FIELD_DEFAULT_SAMPLER]   = "sampler",
    [NGLI_INFO_FIELD_COORDINATE_MATRIX] = "coord_matrix",
    [NGLI_INFO_FIELD_COLOR_MATRIX]      = "color_matrix",
    [NGLI_INFO_FIELD_DIMENSIONS]        = "dimensions",
    [NGLI_INFO_FIELD_TIMESTAMP]         = "ts",
    [NGLI_INFO_FIELD_OES_SAMPLER]       = "external_sampler",
    [NGLI_INFO_FIELD_Y_SAMPLER]         = "y_sampler",
    [NGLI_INFO_FIELD_UV_SAMPLER]        = "uv_sampler",
    [NGLI_INFO_FIELD_Y_RECT_SAMPLER]    = "y_rect_sampler",
    [NGLI_INFO_FIELD_UV_RECT_SAMPLER]   = "uv_rect_sampler",
};

static const int texture_types_map[][NGLI_INFO_FIELD_NB] = {
    [NGLI_PGCRAFT_SHADER_TEX_TYPE_TEXTURE2D] = {
        [NGLI_INFO_FIELD_DEFAULT_SAMPLER]   = NGLI_TYPE_SAMPLER_2D,
        [NGLI_INFO_FIELD_COORDINATE_MATRIX] = NGLI_TYPE_MAT4,
        [NGLI_INFO_FIELD_DIMENSIONS]        = NGLI_TYPE_VEC2,
        [NGLI_INFO_FIELD_TIMESTAMP]         = NGLI_TYPE_FLOAT,
#if !defined(VULKAN_BACKEND)
#if defined(TARGET_ANDROID)
        [NGLI_INFO_FIELD_SAMPLING_MODE]     = NGLI_TYPE_INT,
        [NGLI_INFO_FIELD_OES_SAMPLER]       = NGLI_TYPE_SAMPLER_EXTERNAL_OES,
#elif defined(TARGET_IPHONE) || defined(TARGET_LINUX)
        [NGLI_INFO_FIELD_SAMPLING_MODE]     = NGLI_TYPE_INT,
        [NGLI_INFO_FIELD_Y_SAMPLER]         = NGLI_TYPE_SAMPLER_2D,
        [NGLI_INFO_FIELD_UV_SAMPLER]        = NGLI_TYPE_SAMPLER_2D,
        [NGLI_INFO_FIELD_COLOR_MATRIX]      = NGLI_TYPE_MAT4,
#elif defined(TARGET_DARWIN)
        [NGLI_INFO_FIELD_SAMPLING_MODE]     = NGLI_TYPE_INT,
        [NGLI_INFO_FIELD_Y_RECT_SAMPLER]    = NGLI_TYPE_SAMPLER_2D_RECT,
        [NGLI_INFO_FIELD_UV_RECT_SAMPLER]   = NGLI_TYPE_SAMPLER_2D_RECT,
        [NGLI_INFO_FIELD_COLOR_MATRIX]      = NGLI_TYPE_MAT4,
#endif
#endif
    },
    [NGLI_PGCRAFT_SHADER_TEX_TYPE_IMAGE2D] = {
        [NGLI_INFO_FIELD_DEFAULT_SAMPLER]   = NGLI_TYPE_IMAGE_2D,
        [NGLI_INFO_FIELD_COORDINATE_MATRIX] = NGLI_TYPE_MAT4,
        [NGLI_INFO_FIELD_DIMENSIONS]        = NGLI_TYPE_VEC2,
        [NGLI_INFO_FIELD_TIMESTAMP]         = NGLI_TYPE_FLOAT,
    },
    [NGLI_PGCRAFT_SHADER_TEX_TYPE_TEXTURE3D] = {
        [NGLI_INFO_FIELD_DEFAULT_SAMPLER]   = NGLI_TYPE_SAMPLER_3D,
        [NGLI_INFO_FIELD_DIMENSIONS]        = NGLI_TYPE_VEC3,
    },
    [NGLI_PGCRAFT_SHADER_TEX_TYPE_CUBE] = {
        [NGLI_INFO_FIELD_DEFAULT_SAMPLER]   = NGLI_TYPE_SAMPLER_CUBE,
    },
};

NGLI_STATIC_ASSERT(tex_type_maps_size, NGLI_ARRAY_NB(texture_types_map) == NB_NGLI_TEX_TYPE);

static void prepare_texture_info_fields(struct pgcraft *s, const struct pgcraft_params *params, int graphics,
                                        const struct pgcraft_named_texture *texture,
                                        struct pgcraft_texture_info *info)
{
    const int *types_map = texture_types_map[texture->type];

    for (int i = 0; i < NGLI_INFO_FIELD_NB; i++) {
        struct pgcraft_texture_info_field *field = &info->fields[i];

        field->type = types_map[i];
        if (field->type == NGLI_TYPE_NONE)
            continue;
        snprintf(field->name, sizeof(field->name), "%s_%s", texture->name, texture_info_suffixes[i]);
        if (graphics && i == NGLI_INFO_FIELD_COORDINATE_MATRIX)
            field->stage = NGLI_PROGRAM_SHADER_VERT;
        else
            field->stage = texture->stage;
    }
}

/*
 * A single texture info can be shared between multiple stage, so we need to a
 * first pass to allocate them with and make them hold all the information
 * needed for the following injection stage.
 * FIXME: this means the internally exposed pgcraft_texture_info contains many
 * unnecessary stuff for our users of the pgcraft API.
 */
static int prepare_texture_infos(struct pgcraft *s, const struct pgcraft_params *params, int graphics)
{
    for (int i = 0; i < params->nb_textures; i++) {
        const struct pgcraft_named_texture *texture = &params->textures[i];
        struct pgcraft_texture_info info = {
            .stage     = texture->stage,
            .precision = texture->precision,
            .texture   = texture->texture,
            .image     = texture->image,
            .format    = texture->format,
            .writable  = texture->writable,
        };

        prepare_texture_info_fields(s, params, graphics, texture, &info);

        if (!ngli_darray_push(&s->texture_infos, &info))
            return NGL_ERROR_MEMORY;
    }
    return 0;
}

static int inject_texture_info(struct pgcraft *s, int stage, struct pgcraft_texture_info *info)
{
    for (int i = 0; i < NGLI_INFO_FIELD_NB; i++) {
        const struct pgcraft_texture_info_field *field = &info->fields[i];

        if (field->type == NGLI_TYPE_NONE || field->stage != stage)
            continue;

        struct bstr *b = s->shaders[stage];

        if (ngli_type_is_sampler_or_image(field->type)) {
            struct pipeline_texture pl_texture = {
                .type     = field->type,
                .location = -1,
                .binding  = -1,
                .stage    = stage,
                .texture  = info->texture,
            };
            snprintf(pl_texture.name, sizeof(pl_texture.name), "%s", field->name);

            int *next_bind = s->next_bindings[BIND_ID(stage, BINDING_TYPE_TEXTURE)];
            if (next_bind)
                pl_texture.binding = (*next_bind)++;

            if (field->type == NGLI_TYPE_IMAGE_2D) {
                if (info->format == NGLI_TYPE_NONE) {
                    LOG(ERROR, "Texture2D.format must be set when accessing it as an image");
                    return NGL_ERROR_INVALID_ARG;
                }
                const char *format = ngli_format_get_glsl_format(info->format);
                if (!format) {
                    LOG(ERROR, "unsupported texture format");
                    return NGL_ERROR_UNSUPPORTED;
                }

                ngli_bstr_printf(b, "layout(%s", format);
                if (pl_texture.binding != -1)
                    ngli_bstr_printf(b, "binding=%d", pl_texture.binding);
                ngli_bstr_printf(b, ") %s ", info->writable ? "writeonly" : "readonly");
            } else if (pl_texture.binding != -1) {
                ngli_bstr_printf(b, "layout(binding=%d) ", pl_texture.binding);
            }

            const char *type = ngli_type_get_glsl_type(field->type);
            const char *precision = get_precision_qualifier(s, info->precision);
            if (!precision)
                precision = "lowp";
            ngli_bstr_printf(b, "uniform %s %s %s;\n", precision, type, field->name);

            if (!ngli_darray_push(&s->pipeline_textures, &pl_texture))
                return NGL_ERROR_MEMORY;
        } else {
            struct pgcraft_named_uniform uniform = {
                .stage = field->stage,
                .type = field->type,
            };
            snprintf(uniform.name, sizeof(uniform.name), "%s", field->name);
            int ret = inject_uniform(s, b, stage, &uniform);
            if (ret < 0)
                return ret;
        }
    }
    return 0;
}

static void inject_texture_infos(struct pgcraft *s, int stage, const struct pgcraft_params *params)
{
    struct darray *texture_infos_array = &s->texture_infos;
    struct pgcraft_texture_info *texture_infos = ngli_darray_data(texture_infos_array);
    for (int i = 0; i < ngli_darray_count(texture_infos_array); i++) {
        struct pgcraft_texture_info *info = &texture_infos[i];
        inject_texture_info(s, stage, info);
    }
}

static const char *glsl_layout_str_map[] = {
    [NGLI_BLOCK_LAYOUT_STD140] = "std140",
    [NGLI_BLOCK_LAYOUT_STD430] = "std430",
};

static int inject_block(struct pgcraft *s, struct bstr *b, int stage,
                        const struct pgcraft_named_block *named_block)
{
    if (named_block->stage != stage)
        return 0;

    const struct block *block = named_block->block;

    struct pipeline_buffer pl_buffer = {
        .type    = block->type,
        .binding = -1,
        .stage   = stage,
        .buffer  = named_block->buffer,
    };
    snprintf(pl_buffer.name, sizeof(pl_buffer.name), "%s_block", named_block->name);

    const char *layout = glsl_layout_str_map[block->layout];
    const int bind_type = block->type == NGLI_TYPE_UNIFORM_BUFFER ? BINDING_TYPE_UBO : BINDING_TYPE_SSBO;
    int *next_bind = s->next_bindings[BIND_ID(stage, bind_type)];
    if (next_bind) {
        pl_buffer.binding = (*next_bind)++;
        ngli_bstr_printf(b, "layout(%s,binding=%d)", layout, pl_buffer.binding);
    } else {
        ngli_bstr_printf(b, "layout(%s)", layout);
    }

    const char *keyword = ngli_type_get_glsl_type(block->type);
    ngli_bstr_printf(b, " %s %s_block {\n", keyword, named_block->name);
    const struct block_field *field_info = ngli_darray_data(&block->fields);
    for (int i = 0; i < ngli_darray_count(&block->fields); i++) {
        const struct block_field *fi = &field_info[i];
        const char *type = ngli_type_get_glsl_type(fi->type);
        if (named_block->variadic && fi->count && i == ngli_darray_count(&block->fields))
            ngli_bstr_printf(b, "    %s %s[];\n", type, fi->name);
        else if (fi->count)
            ngli_bstr_printf(b, "    %s %s[%d];\n", type, fi->name, fi->count);
        else
            ngli_bstr_printf(b, "    %s %s;\n", type, fi->name);
    }
    const char *instance_name = named_block->instance_name ? named_block->instance_name : named_block->name;
    ngli_bstr_printf(b, "} %s;\n", instance_name);

    if (!ngli_darray_push(&s->pipeline_buffers, &pl_buffer))
        return NGL_ERROR_MEMORY;
    return 0;
}

static int inject_attribute(struct pgcraft *s, struct bstr *b, int stage,
                            const struct pgcraft_named_attribute *attribute)
{
    ngli_assert(stage == NGLI_PROGRAM_SHADER_VERT);

    const char *type = ngli_type_get_glsl_type(attribute->type);
    const char *precision = get_precision_qualifier(s, attribute->precision);

    if (!precision)
        precision = "highp";

    int base_location = -1;
    const int attribute_count = attribute->type == NGLI_TYPE_MAT4 ? 4 : 1;
    if (s->next_in_location) {
        base_location = s->next_in_location[stage];
        s->next_in_location[stage] += attribute_count;
        ngli_bstr_printf(b, "layout(location=%d) ", base_location);
    }

    /*
     * If an attribute is declared but has no data, we still need to inject a
     * dummy one in the shader (without registering a pipeline entry) so that
     * the shader compilation works.
     */
    const char *qualifier = s->has_in_out_qualifiers ? "in" : "varying";
    ngli_bstr_printf(b, "%s %s %s %s;\n", qualifier, precision, type, attribute->name);
    if (!attribute->buffer)
        return 0;

    const int attribute_offset = ngli_format_get_bytes_per_pixel(attribute->format);
    for (int i = 0; i < attribute_count; i++) {
        /* negative location offset trick is for probe_pipeline_attribute() */
        const int loc = base_location != -1 ? base_location + i : -1 - i;
        struct pipeline_attribute pl_attribute = {
            .location = loc,
            .format   = attribute->format,
            .stride   = attribute->stride,
            .offset   = attribute->offset + i * attribute_offset,
            .rate     = attribute->rate,
            .buffer   = attribute->buffer,
        };
        snprintf(pl_attribute.name, sizeof(pl_attribute.name), "%s", attribute->name);

        if (!ngli_darray_push(&s->pipeline_attributes, &pl_attribute))
            return NGL_ERROR_MEMORY;
    }

    return 0;
}

#define DEFINE_INJECT_FUNC(e)                                               \
static int inject_##e##s(struct pgcraft *s, struct bstr *b, int stage,      \
                         const struct pgcraft_params *params)               \
{                                                                           \
    for (int i = 0; i < params->nb_##e##s; i++) {                           \
        int ret = inject_##e(s, b, stage, &params->e##s[i]);                \
        if (ret < 0)                                                        \
            return ret;                                                     \
    }                                                                       \
    return 0;                                                               \
}

DEFINE_INJECT_FUNC(uniform)
DEFINE_INJECT_FUNC(block)
DEFINE_INJECT_FUNC(attribute)

const char *ublock_names[] = {
    [NGLI_PROGRAM_SHADER_VERT] = "vert",
    [NGLI_PROGRAM_SHADER_FRAG] = "frag",
    [NGLI_PROGRAM_SHADER_COMP] = "comp",
};

static int inject_ublock(struct pgcraft *s, struct bstr *b, int stage)
{
    if (!s->use_ublock)
        return 0;

    struct block *block = &s->ublock[stage];
    if (!block->size)
        return 0;

    // FIXME: need to fallback on storage buffer if needed, similarly to pass
    block->type = NGLI_TYPE_UNIFORM_BUFFER;

    struct buffer *ubuffer = &s->ubuffer[stage];
    int ret = ngli_buffer_init(ubuffer, s->ctx, block->size, NGLI_BUFFER_USAGE_DYNAMIC);
    if (ret < 0)
        return ret;

    struct pgcraft_named_block named_block = {
        /* instance name is empty to make field accesses identical to uniform accesses */
        .instance_name = "",
        .stage         = stage,
        .block         = block,
        .buffer        = ubuffer,
    };
    snprintf(named_block.name, sizeof(named_block.name), "ngl_%s", ublock_names[stage]);

    return inject_block(s, b, stage, &named_block);
}

static void set_glsl_header(struct pgcraft *s, struct bstr *b)
{
    ngli_bstr_printf(b, "#version %d%s\n", s->glsl_version, s->glsl_version_suffix);

    if (ngli_darray_data(&s->texture_infos)) {
#if defined(TARGET_ANDROID)
        ngli_bstr_print(b, "#extension GL_OES_EGL_image_external : require\n");
#endif

        /* Define Internal/private raw texture picking. The ngl_* versions are
         * defined in samplers_preproc() */
        if (s->has_modern_texture_picking)
            ngli_bstr_print(b, "#define ngli_tex2d   texture\n"
                               "#define ngli_tex3d   texture\n"
                               "#define ngli_texcube texture\n");
        else
            ngli_bstr_print(b, "#define ngli_tex2d   texture2D\n"
                               "#define ngli_tex3d   texture3D\n"
                               "#define ngli_texcube textureCube\n");
        ngli_bstr_print(b, "#define ngli_img2d  imageLoad\n"
                           "#define ngli_imgsz  imageSize\n"
                           "#define ngli_texlod textureLod\n");
    }

    ngli_bstr_print(b, "\n");
}

static const char * const simple_ngl_picking_funcs[] = {
    "ngl_img2d", "ngl_imgsz", "ngl_texlod",
    "ngl_tex2d", "ngl_tex3d", "ngl_texcube",
};

static int is_simple_ngl_picking_func(const char *s)
{
    for (int i = 0; i < NGLI_ARRAY_NB(simple_ngl_picking_funcs); i++)
        if (!strcmp(simple_ngl_picking_funcs[i], s))
            return 1;
    return 0;
}

#define WHITESPACES     "\r\n\t "
#define TOKEN_ID_CHARS  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_"

static const char *read_token_id(const char *p, char *buf, size_t size)
{
    const size_t len = strspn(p, TOKEN_ID_CHARS);
    snprintf(buf, size, "%.*s", (int)len, p);
    return p + len;
}

static const char *skip_arg(const char *p)
{
    /*
     * TODO: need to error out on directive lines since evaluating them is too
     * complex (and you could close a '(' in a #ifdef and close again in the
     * #else branch, so it's a problem for us)
     */
    int opened_paren = 0;
    while (*p) {
        if (*p == ',' && !opened_paren) {
            break;
        } else if (*p == '(') {
            opened_paren++;
            p++;
        } else if (*p == ')') {
            if (opened_paren == 0)
                break;
            opened_paren--;
            p++;
        } else if (!strncmp(p, "//", 2)) {
            p += strcspn(p, "\r\n");
            // TODO: skip to EOL (handle '\' at EOL?)
        } else if (!strncmp(p, "/*", 2)) {
            p += 2;
            const char *eoc = strstr(p, "*/");
            if (eoc)
                p = eoc + 2;
        } else {
            p++;
        }
    }
    return p;
}

struct token {
    char id[16];
    ptrdiff_t pos;
};

#define ARG_FMT(x) (int)x##_len, x##_start

static int handle_token(struct pgcraft *s, const struct token *token, const char *p, struct bstr *dst)
{
    /* Skip "ngl_XXX(" and the whitespaces */
    p += strlen(token->id);
    p += strspn(p, WHITESPACES);
    if (*p++ != '(')
        return NGL_ERROR_INVALID_ARG;
    p += strspn(p, WHITESPACES);

    /* Extract the first argument (texture base name) from which we later
     * derive all the uniform names */
    const char *arg0_start = p;
    p = skip_arg(p);
    ptrdiff_t arg0_len = p - arg0_start;

    /* The internal ngli_texvideo() is an internal fast-path to skip the check
     * for the sampling mode and directly do the picking */
    const int fast_picking = !strcmp(token->id, "ngli_texvideo");

    if (fast_picking || !strcmp(token->id, "ngl_texvideo")) {
        if (*p != ',')
            return NGL_ERROR_INVALID_ARG;
        p++;
        p += strspn(p, WHITESPACES);

        const char *coords_start = p;
        p = skip_arg(p);
        ptrdiff_t coords_len = p - coords_start;
        if (*p != ')')
            return NGL_ERROR_INVALID_ARG;
        p++;

        ngli_bstr_print(dst, "(");
#if defined VULKAN_BACKEND
        ngli_bstr_printf(dst, "ngli_tex2d(%.*s_sampler, %.*s)", ARG_FMT(arg0), ARG_FMT(coords));
#else
#if defined(TARGET_ANDROID)
        if (!fast_picking)
            ngli_bstr_printf(dst, "%.*s_sampling_mode == 2 ? ", ARG_FMT(arg0));
        ngli_bstr_printf(dst, "ngli_tex2d(%.*s_external, %.*s)", ARG_FMT(arg0), ARG_FMT(coords));
        if (!fast_picking)
            ngli_bstr_printf(dst, " : ngli_tex2d(%.*s_sampler, %.*s)", ARG_FMT(arg0), ARG_FMT(coords));
#elif defined(TARGET_IPHONE) || defined(TARGET_LINUX)
        if (!fast_picking)
            ngli_bstr_printf(dst, "%.*s_sampling_mode == 3 ? ", ARG_FMT(arg0));
        ngli_bstr_printf(dst, "%.*s_color_matrix * vec4(ngli_tex2d(%.*s_y_sampler,  %.*s).r,"
                                                       "ngli_tex2d(%.*s_uv_sampler, %.*s).%s, 1.0)",
                         ARG_FMT(arg0),
                         ARG_FMT(arg0), ARG_FMT(coords),
                         ARG_FMT(arg0), ARG_FMT(coords), s->rg);
        if (!fast_picking)
            ngli_bstr_printf(dst, " : ngli_tex2d(%.*s_sampler, %.*s)", ARG_FMT(arg0), ARG_FMT(coords));
#elif defined(TARGET_DARWIN)
        if (!fast_picking)
            ngli_bstr_printf(dst, "%.*s_sampling_mode == 4 ? ", ARG_FMT(arg0));
        ngli_bstr_printf(dst, "%.*s_color_matrix * vec4(ngli_tex2d(%.*s_y_rect_sampler,  (%.*s) * %.*s_dimensions / 2.0).r,"
                                                       "ngli_tex2d(%.*s_uv_rect_sampler, (%.*s) * %.*s_dimensions / 2.0).rg, 1.0)",
                         ARG_FMT(arg0),
                         ARG_FMT(arg0), ARG_FMT(coords), ARG_FMT(arg0),
                         ARG_FMT(arg0), ARG_FMT(coords), ARG_FMT(arg0));
        if (!fast_picking)
            ngli_bstr_printf(dst, " : ngli_tex2d(%.*s_sampler, %.*s)", ARG_FMT(arg0), ARG_FMT(coords));
#else
        ngli_bstr_printf(dst, "ngli_tex2d(%.*s_sampler, %.*s)", ARG_FMT(arg0), ARG_FMT(coords));
#endif
#endif
        ngli_bstr_print(dst, ")");
        ngli_bstr_print(dst, p);
    } else if (is_simple_ngl_picking_func(token->id)) {
        ngli_bstr_printf(dst, "ngli_%s(%.*s_sampler%s", token->id + 4, ARG_FMT(arg0), p);
    } else {
        ngli_assert(0);
    }
    return 0;
}

/*
 * We can not make use of the GLSL preproc to create these custom ngl_*()
 * operators because token pasting (##) is needed but illegal in GLES.
 *
 * Implementing a complete preprocessor is too much of a hassle and risky,
 * especially since we need to evaluate all directives in addition to ours.
 * Instead, we do a simple search & replace for our custom texture helpers. We
 * make sure it supports basic nesting, but aside from that, it's pretty basic.
 */
static int samplers_preproc(struct pgcraft *s, struct bstr *b)
{
    /*
     * If there is no texture, no point in looking for these custom "ngl_"
     * texture picking symbols.
     */
    if (!ngli_darray_data(&s->texture_infos))
        return 0;

    struct bstr *tmp_buf = ngli_bstr_create();
    if (!tmp_buf)
        return NGL_ERROR_MEMORY;

    /*
     * Construct a stack of "ngl_*" tokens found in the shader.
     */
    struct darray token_stack;
    ngli_darray_init(&token_stack, sizeof(struct token), 0);
    const char *base_str = ngli_bstr_strptr(b);
    const char *p = base_str;
    while ((p = strstr(p, "ngl"))) {
        struct token token = {.pos = p - base_str};
        p = read_token_id(p, token.id, sizeof(token.id));
        if (!is_simple_ngl_picking_func(token.id) &&
            strcmp(token.id, "ngl_texvideo") &&
            strcmp(token.id, "ngli_texvideo"))
            continue;
        ngli_darray_push(&token_stack, &token);
    }

    /*
     * Read and process the stack from the bottom-up so that we know there is
     * never anything left to substitute up until the end of the buffer.
     */
    int ret = 0;
    const struct token *tokens = ngli_darray_data(&token_stack);
    const int nb_tokens = ngli_darray_count(&token_stack);
    for (int i = 0; i < nb_tokens; i++) {
        const struct token *token = &tokens[nb_tokens - i - 1];
        ngli_bstr_clear(tmp_buf);

        /*
         * We get back the pointer in case it changed in a previous iteration
         * (internal realloc while extending it). The token offset on the other
         * hand wouldn't change since we're doing the replacements backward.
         */
        p = ngli_bstr_strptr(b);
        ret = handle_token(s, token, p + token->pos, tmp_buf);
        if (ret < 0)
            break;

        /*
         * The token function did print into the temporary buffer everything
         * up until the end of the buffer, so we can just truncate the main
         * buffer, and re-append the new payload.
         */
        ngli_bstr_truncate(b, token->pos);
        ngli_bstr_print(b, ngli_bstr_strptr(tmp_buf));
    }

    ngli_darray_reset(&token_stack);
    ngli_bstr_freep(&tmp_buf);
    return ret;
}

static int inject_vert2frags(struct pgcraft *s, struct bstr *b, int stage)
{
    static const char *qualifiers[2][2] = {
        [NGLI_PROGRAM_SHADER_VERT] = {"varying", "out"},
        [NGLI_PROGRAM_SHADER_FRAG] = {"varying", "in"},
    };
    const char *qualifier = qualifiers[stage][s->has_in_out_qualifiers];
    const struct pgcraft_named_iovar *iovars = ngli_darray_data(&s->vert2frag_vars);
    for (int i = 0; i < ngli_darray_count(&s->vert2frag_vars); i++) {
        if (s->has_in_out_qualifiers) // Note: sometimes we can have in/out without layout location
            ngli_bstr_printf(b, "layout(location=%d) ", i);
        const struct pgcraft_named_iovar *iovar = &iovars[i];
        const char *type = ngli_type_get_glsl_type(iovar->type);
        ngli_bstr_printf(b, "%s %s %s;\n", qualifier, type, iovar->name);
    }
    return 0;
}

static int craft_vert(struct pgcraft *s, const struct pgcraft_params *params)
{
    struct bstr *b = s->shaders[NGLI_PROGRAM_SHADER_VERT];

    set_glsl_header(s, b);

    ngli_bstr_print(b, "#define ngl_out_pos gl_Position\n");

    inject_vert2frags(s, b, NGLI_PROGRAM_SHADER_VERT);
    inject_uniforms(s, b, NGLI_PROGRAM_SHADER_VERT, params);
    inject_texture_infos(s, NGLI_PROGRAM_SHADER_VERT, params);
    inject_blocks(s, b, NGLI_PROGRAM_SHADER_VERT, params);
    inject_attributes(s, b,  NGLI_PROGRAM_SHADER_VERT, params);
    inject_ublock(s, b, NGLI_PROGRAM_SHADER_VERT);

    ngli_bstr_print(b, params->vert_base);
    return samplers_preproc(s, b);
}

static int craft_frag(struct pgcraft *s, const struct pgcraft_params *params)

{
    struct bstr *b = s->shaders[NGLI_PROGRAM_SHADER_FRAG];

    set_glsl_header(s, b);

    if (s->has_precision_qualifiers)
        ngli_bstr_print(b, "#if GL_FRAGMENT_PRECISION_HIGH\n"
                           "precision highp float;\n"
                           "#else\n"
                           "precision mediump float;\n"
                           "#endif\n");

    if (s->has_in_out_qualifiers) {
        if (s->next_out_location) {
            const int out_location = s->next_out_location[NGLI_PROGRAM_SHADER_FRAG]++;
            ngli_bstr_printf(b, "layout(location=%d) ", out_location);
        }
        if (params->nb_frag_output)
            ngli_bstr_printf(b, "out vec4 ngl_out_color[%d];\n", params->nb_frag_output);
        else
            ngli_bstr_print(b, "out vec4 ngl_out_color;\n");
    } else {
        ngli_bstr_print(b, "#define ngl_out_color gl_FragColor\n");
    }

    inject_vert2frags(s, b, NGLI_PROGRAM_SHADER_FRAG);
    inject_uniforms(s, b, NGLI_PROGRAM_SHADER_FRAG, params);
    inject_texture_infos(s, NGLI_PROGRAM_SHADER_FRAG, params);
    inject_blocks(s, b, NGLI_PROGRAM_SHADER_FRAG, params);
    inject_ublock(s, b, NGLI_PROGRAM_SHADER_FRAG);

    ngli_bstr_print(b, params->frag_base);
    return samplers_preproc(s, b);
}

static int craft_comp(struct pgcraft *s, const struct pgcraft_params *params)
{
    struct bstr *b = s->shaders[NGLI_PROGRAM_SHADER_COMP];

    set_glsl_header(s, b);

    inject_uniforms(s, b, NGLI_PROGRAM_SHADER_COMP, params);
    inject_texture_infos(s, NGLI_PROGRAM_SHADER_COMP, params);
    inject_blocks(s, b, NGLI_PROGRAM_SHADER_COMP, params);
    inject_ublock(s, b, NGLI_PROGRAM_SHADER_COMP);
    ngli_bstr_print(b, params->comp_base);
    return samplers_preproc(s, b);
}

/* XXX: pipeline uniforms have no location */
static int probe_pipeline_uniform(const struct hmap *info_map, void *arg)
{
    struct pipeline_uniform *elem = arg;
    const struct program_variable_info *info = ngli_hmap_get(info_map, elem->name);
    if (!info)
        return NGL_ERROR_NOT_FOUND;
    return 0;
}

static int probe_pipeline_buffer(const struct hmap *info_map, void *arg)
{
    struct pipeline_buffer *elem = arg;
    if (elem->binding != -1)
        return 0;
    const struct program_variable_info *info = ngli_hmap_get(info_map, elem->name);
    if (!info)
        return NGL_ERROR_NOT_FOUND;
    elem->binding = info->binding;
    return elem->binding != -1 ? 0 : NGL_ERROR_NOT_FOUND;
}

/* XXX: hardcheck binding != -1 for images? */
static int probe_pipeline_texture(const struct hmap *info_map, void *arg)
{
    struct pipeline_texture *elem = arg;
    if (elem->location != -1)
        return 0;
    const struct program_variable_info *info = ngli_hmap_get(info_map, elem->name);
    if (!info)
        return NGL_ERROR_NOT_FOUND;
    elem->location = info->location;
    if (elem->binding == -1)
        elem->binding = info->binding;
    return elem->location != -1 ? 0 : NGL_ERROR_NOT_FOUND;
}

static int probe_pipeline_attribute(const struct hmap *info_map, void *arg)
{
    struct pipeline_attribute *elem = arg;
    if (elem->location >= 0) // can be ≤ -1 if there is a location offset so we don't check ≠ -1 here
        return 0;
    const struct program_variable_info *info = ngli_hmap_get(info_map, elem->name);
    if (!info || info->location == -1)
        return NGL_ERROR_NOT_FOUND;
    const int loc_offset = -elem->location - 1; // reverse location offset trick from inject_attribute()
    elem->location = info->location + loc_offset;
    return 0;
}

typedef int (*probe_func_type)(const struct hmap *info_map, void *arg);

static int filter_pipeline_elems(struct pgcraft *s, probe_func_type probe_func,
                                 const struct hmap *info_map,
                                 struct darray *src, struct darray *dst)
{
    uint8_t *elems = ngli_darray_data(src);
    for (int i = 0; i < ngli_darray_count(src); i++) {
        void *elem = elems + i * src->element_size;
        if (info_map && probe_func(info_map, elem) < 0)
            continue;
        if (!ngli_darray_push(dst, elem))
            return NGL_ERROR_MEMORY;
    }
    ngli_darray_reset(src);
    return 0;
}

static int get_uniform_index(const struct pgcraft *s, const char *name)
{
    const struct pipeline_uniform *pipeline_uniforms = ngli_darray_data(&s->filtered_pipeline_uniforms);
    for (int i = 0; i < ngli_darray_count(&s->filtered_pipeline_uniforms); i++) {
        const struct pipeline_uniform *pipeline_uniform = &pipeline_uniforms[i];
        if (!strcmp(pipeline_uniform->name, name))
            return i;
    }
    return -1;
}

static int get_texture_index(const struct pgcraft *s, const char *name)
{
    const struct pipeline_texture *pipeline_textures = ngli_darray_data(&s->filtered_pipeline_textures);
    for (int i = 0; i < ngli_darray_count(&s->filtered_pipeline_textures); i++) {
        const struct pipeline_texture *pipeline_texture = &pipeline_textures[i];
        if (!strcmp(pipeline_texture->name, name))
            return i;
    }
    return -1;
}

static int get_ublock_index(const struct pgcraft *s, const char *name, int stage)
{
    const struct darray *fields_array = &s->ublock[stage].fields;
    const struct block_field *fields = ngli_darray_data(fields_array);
    for (int i = 0; i < ngli_darray_count(fields_array); i++)
        if (!strcmp(fields[i].name, name))
            return stage << 16 | i;
    return -1;
}

static void probe_texture_info_elems(const struct pgcraft *s, struct pgcraft_texture_info_field *fields)
{
    for (int i = 0; i < NGLI_INFO_FIELD_NB; i++) {
        struct pgcraft_texture_info_field *field = &fields[i];
        if (field->type == NGLI_TYPE_NONE)
            field->index = -1;
        else if (ngli_type_is_sampler_or_image(field->type))
            field->index = get_texture_index(s, field->name);
        else
            field->index = s->use_ublock ? get_ublock_index(s, field->name, field->stage): get_uniform_index(s, field->name);
    }
}

static void probe_texture_infos(struct pgcraft *s)
{
    struct darray *texture_infos_array = &s->texture_infos;
    struct pgcraft_texture_info *texture_infos = ngli_darray_data(texture_infos_array);
    for (int i = 0; i < ngli_darray_count(texture_infos_array); i++) {
        struct pgcraft_texture_info *info = &texture_infos[i];
        probe_texture_info_elems(s, info->fields);
    }
}

/*
 * Fill location/binding of pipeline params if they are not set by probing the
 * shader. Also fill the filtered array with available entries.
 */
static int probe_pipeline_elems(struct pgcraft *s)
{
    int ret;

    const struct hmap *uniforms_info   = s->program.uniforms;
    const struct hmap *buffers_info    = s->program.buffer_blocks;
    const struct hmap *attributes_info = s->program.attributes;

    if ((ret = filter_pipeline_elems(s, probe_pipeline_uniform,   uniforms_info,   &s->pipeline_uniforms,   &s->filtered_pipeline_uniforms))   < 0 ||
        (ret = filter_pipeline_elems(s, probe_pipeline_buffer,    buffers_info,    &s->pipeline_buffers,    &s->filtered_pipeline_buffers))    < 0 ||
        (ret = filter_pipeline_elems(s, probe_pipeline_texture,   uniforms_info,   &s->pipeline_textures,   &s->filtered_pipeline_textures))   < 0 ||
        (ret = filter_pipeline_elems(s, probe_pipeline_attribute, attributes_info, &s->pipeline_attributes, &s->filtered_pipeline_attributes)) < 0)
        return ret;

    probe_texture_infos(s);
    return 0;
}

static int alloc_shader(struct pgcraft *s, int stage)
{
    ngli_assert(!s->shaders[stage]);
    struct bstr *b = ngli_bstr_create();
    if (!b)
        return NGL_ERROR_MEMORY;
    s->shaders[stage] = b;
    return 0;
}

#ifdef VULKAN_BACKEND
static void setup_glsl_info(struct pgcraft *s, const struct vkcontext *vk)
{
    s->rg = "rg";
    s->glsl_version = 450;
    s->glsl_version_suffix = "";

    // XXX
    s->has_in_out_qualifiers = 1;
    s->has_precision_qualifiers = 0;
    s->has_modern_texture_picking = 1;
    s->has_buffer_bindings = 1;

    s->use_ublock = 1;
    s->has_shared_bindings = 1;

    if (s->has_buffer_bindings) {
        if (s->has_shared_bindings)
            for (int i = 0; i < NB_BINDINGS; i++)
                s->next_bindings[i] = &s->bindings[0];
        else
            for (int i = 0; i < NB_BINDINGS; i++)
                s->next_bindings[i] = &s->bindings[i];
    }

    s->next_in_location = s->in_locations;
    s->next_out_location = s->out_locations;
}
#else
#define IS_GL_ES_MIN(min)   (gl->backend == NGL_BACKEND_OPENGLES && gl->version >= (min))
#define IS_GL_MIN(min)      (gl->backend == NGL_BACKEND_OPENGL   && gl->version >= (min))
#define IS_GLSL_ES_MIN(min) (gl->backend == NGL_BACKEND_OPENGLES && s->glsl_version >= (min))
#define IS_GLSL_MIN(min)    (gl->backend == NGL_BACKEND_OPENGL   && s->glsl_version >= (min))

static void setup_glsl_info(struct pgcraft *s, const struct glcontext *gl)
{
    s->rg = "rg";

    if (gl->backend == NGL_BACKEND_OPENGL) {
        switch (gl->version) {
        case 300: s->glsl_version = 130;         break;
        case 310: s->glsl_version = 140;         break;
        case 320: s->glsl_version = 150;         break;
        default:  s->glsl_version = gl->version; break;
        }
        s->glsl_version_suffix = "";
    } else if (gl->backend == NGL_BACKEND_OPENGLES) {
        s->glsl_version = gl->version >= 300 ? gl->version : 100;
        s->glsl_version_suffix = " es";
        if (gl->version < 300) // see formats.c
            s->rg = "ra";
    } else {
        ngli_assert(0);
    }

    /* XXX: are these correct? can they be more accurate? */
    s->has_in_out_qualifiers      = IS_GLSL_ES_MIN(300) || IS_GLSL_MIN(150);
    s->has_precision_qualifiers   = IS_GLSL_ES_MIN(130);
    s->has_modern_texture_picking = IS_GLSL_ES_MIN(300) || IS_GLSL_MIN(330);
    s->has_buffer_bindings        = IS_GL_ES_MIN(310) || IS_GL_MIN(420);

    s->has_shared_bindings = 0;

    if (s->has_buffer_bindings) {
        if (s->has_shared_bindings)
            for (int i = 0; i < NB_BINDINGS; i++)
                s->next_bindings[i] = &s->bindings[0];
        else
            for (int i = 0; i < NB_BINDINGS; i++)
                s->next_bindings[i] = &s->bindings[i];
    }

    /*
     * FIXME: currently, program probing code forces a binding for the UBO, so
     * it directly conflicts with the indexes we could set here.
     */
    s->next_bindings[BIND_ID(NGLI_PROGRAM_SHADER_VERT, BINDING_TYPE_UBO)] = NULL;
    s->next_bindings[BIND_ID(NGLI_PROGRAM_SHADER_FRAG, BINDING_TYPE_UBO)] = NULL;
    s->next_bindings[BIND_ID(NGLI_PROGRAM_SHADER_COMP, BINDING_TYPE_UBO)] = NULL;

    s->next_bindings[BIND_ID(NGLI_PROGRAM_SHADER_VERT, BINDING_TYPE_TEXTURE)] = NULL;
    s->next_bindings[BIND_ID(NGLI_PROGRAM_SHADER_FRAG, BINDING_TYPE_TEXTURE)] = NULL;
    s->next_bindings[BIND_ID(NGLI_PROGRAM_SHADER_COMP, BINDING_TYPE_TEXTURE)] = NULL;

    s->use_ublock = 0;

    // XXX: should we add in/out location for gl when available?
    s->next_in_location = NULL;
    s->next_out_location = NULL;
}
#endif

int ngli_pgcraft_init(struct pgcraft *s, struct ngl_ctx *ctx)
{
    memset(s, 0, sizeof(*s));

#ifdef VULKAN_BACKEND
    setup_glsl_info(s, ctx->vkcontext);
#else
    setup_glsl_info(s, ctx->glcontext);
#endif

    if (s->use_ublock)
        ngli_block_init(s->ublock, NGLI_BLOCK_LAYOUT_STD140);

    s->ctx = ctx;

    ngli_darray_init(&s->texture_infos, sizeof(struct pgcraft_texture_info), 0);

    ngli_darray_init(&s->pipeline_uniforms,   sizeof(struct pipeline_uniform),   0);
    ngli_darray_init(&s->pipeline_textures,   sizeof(struct pipeline_texture),   0);
    ngli_darray_init(&s->pipeline_buffers,    sizeof(struct pipeline_buffer),    0);
    ngli_darray_init(&s->pipeline_attributes, sizeof(struct pipeline_attribute), 0);

    ngli_darray_init(&s->filtered_pipeline_uniforms,   sizeof(struct pipeline_uniform),   0);
    ngli_darray_init(&s->filtered_pipeline_textures,   sizeof(struct pipeline_texture),   0);
    ngli_darray_init(&s->filtered_pipeline_buffers,    sizeof(struct pipeline_buffer),    0);
    ngli_darray_init(&s->filtered_pipeline_attributes, sizeof(struct pipeline_attribute), 0);

    return 0;
}

static int get_program_compute(struct pgcraft *s, const struct pgcraft_params *params)
{
    int ret;

    if ((ret = alloc_shader(s, NGLI_PROGRAM_SHADER_COMP)) < 0 ||
        (ret = prepare_texture_infos(s, params, 0)) < 0 ||
        (ret = craft_comp(s, params)) < 0)
        return ret;

    const char *comp = ngli_bstr_strptr(s->shaders[NGLI_PROGRAM_SHADER_COMP]);
    ret = ngli_pgcache_get_compute_program(&s->ctx->pgcache, &s->program, comp);
    ngli_bstr_freep(&s->shaders[NGLI_PROGRAM_SHADER_COMP]);
    return ret;
}

static int get_program_graphics(struct pgcraft *s, const struct pgcraft_params *params)
{
    int ret;

    ngli_darray_init(&s->vert2frag_vars, sizeof(struct pgcraft_named_iovar), 0);
    for (int i = 0; i < params->nb_vert2frag_vars; i++) {
        struct pgcraft_named_iovar *iovar = ngli_darray_push(&s->vert2frag_vars, &params->vert2frag_vars[i]);
        if (!iovar)
            return NGL_ERROR_MEMORY;
    }

    if ((ret = alloc_shader(s, NGLI_PROGRAM_SHADER_VERT)) < 0 ||
        (ret = alloc_shader(s, NGLI_PROGRAM_SHADER_FRAG)) < 0 ||
        (ret = prepare_texture_infos(s, params, 1)) < 0 ||
        (ret = craft_vert(s, params)) < 0 ||
        (ret = craft_frag(s, params)) < 0)
        return ret;

    const char *vert = ngli_bstr_strptr(s->shaders[NGLI_PROGRAM_SHADER_VERT]);
    const char *frag = ngli_bstr_strptr(s->shaders[NGLI_PROGRAM_SHADER_FRAG]);
    ret = ngli_pgcache_get_graphics_program(&s->ctx->pgcache, &s->program, vert, frag);
    ngli_bstr_freep(&s->shaders[NGLI_PROGRAM_SHADER_VERT]);
    ngli_bstr_freep(&s->shaders[NGLI_PROGRAM_SHADER_FRAG]);
    return ret;
}

int ngli_pgcraft_craft(struct pgcraft *s,
                       struct pipeline_params *dst_params,
                       const struct pgcraft_params *params)
{
    int ret = params->comp_base ? get_program_compute(s, params)
                                : get_program_graphics(s, params);
    if (ret < 0)
        return ret;

    ret = probe_pipeline_elems(s);
    if (ret < 0)
        return ret;

    if (s->use_ublock) {
        ngli_assert(dst_params->nb_uniforms == 0);
        for (int i = 0; i < NGLI_ARRAY_NB(s->ublock); i++) {
            struct block *block = &s->ublock[i];
            if (block->size) {
                struct buffer *buffer = &s->ubuffer[i];
                dst_params->ublock[i]  = block;
                dst_params->ubuffer[i] = buffer;
            }
        }
    } else {
        memset(dst_params->ublock, 0, sizeof(dst_params->ublock));
    }

    dst_params->program       = &s->program;
    dst_params->uniforms      = ngli_darray_data(&s->filtered_pipeline_uniforms);
    dst_params->nb_uniforms   = ngli_darray_count(&s->filtered_pipeline_uniforms);
    dst_params->textures      = ngli_darray_data(&s->filtered_pipeline_textures);
    dst_params->nb_textures   = ngli_darray_count(&s->filtered_pipeline_textures);
    dst_params->attributes    = ngli_darray_data(&s->filtered_pipeline_attributes);
    dst_params->nb_attributes = ngli_darray_count(&s->filtered_pipeline_attributes);
    dst_params->buffers       = ngli_darray_data(&s->filtered_pipeline_buffers);
    dst_params->nb_buffers    = ngli_darray_count(&s->filtered_pipeline_buffers);

    return 0;
}

int ngli_pgcraft_get_uniform_index(const struct pgcraft *s, const char *name, int stage)
{
    return s->use_ublock ? get_ublock_index(s, name, stage) : get_uniform_index(s, name);
}

void ngli_pgcraft_reset(struct pgcraft *s)
{
    if (!s->ctx)
        return;

    ngli_darray_reset(&s->texture_infos);
    ngli_darray_reset(&s->vert2frag_vars);

    if (s->use_ublock) {
        for (int i = 0; i < NGLI_ARRAY_NB(s->ublock); i++) {
            ngli_block_reset(&s->ublock[i]);
            ngli_buffer_reset(&s->ubuffer[i]);
        }
    }

    for (int i = 0; i < NGLI_ARRAY_NB(s->shaders); i++)
        ngli_bstr_freep(&s->shaders[i]);
    ngli_pgcache_release_program(&s->program);

    ngli_darray_reset(&s->pipeline_uniforms);
    ngli_darray_reset(&s->pipeline_textures);
    ngli_darray_reset(&s->pipeline_buffers);
    ngli_darray_reset(&s->pipeline_attributes);

    memset(s, 0, sizeof(*s));
}

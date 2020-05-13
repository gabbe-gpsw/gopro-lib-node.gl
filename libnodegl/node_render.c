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

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>

#include "darray.h"
#include "hmap.h"
#include "log.h"
#include "nodegl.h"
#include "nodes.h"
#include "params.h"
#include "pass.h"
#include "topology.h"
#include "type.h"
#include "utils.h"

struct render_priv {
    struct ngl_node *geometry;
    struct ngl_node *program;
    struct hmap *vertex_resources;
    struct hmap *fragment_resources;
    struct hmap *attributes;
    struct hmap *instance_attributes;
    int nb_instances;

    struct pass pass;
    struct darray vert2frag_vars; // pgcraft_named_iovar
};

#define PROGRAMS_TYPES_LIST (const int[]){NGL_NODE_PROGRAM,         \
                                          -1}

#define INPUT_TYPES_LIST    (const int[]){NGL_NODE_TEXTURE2D,       \
                                          NGL_NODE_TEXTURE3D,       \
                                          NGL_NODE_TEXTURECUBE,     \
                                          NGL_NODE_BLOCK,           \
                                          NGL_NODE_BUFFERFLOAT,     \
                                          NGL_NODE_BUFFERVEC2,      \
                                          NGL_NODE_BUFFERVEC3,      \
                                          NGL_NODE_BUFFERVEC4,      \
                                          NGL_NODE_STREAMEDBUFFERINT,   \
                                          NGL_NODE_STREAMEDBUFFERIVEC2, \
                                          NGL_NODE_STREAMEDBUFFERIVEC3, \
                                          NGL_NODE_STREAMEDBUFFERIVEC4, \
                                          NGL_NODE_STREAMEDBUFFERUINT,  \
                                          NGL_NODE_STREAMEDBUFFERUIVEC2,\
                                          NGL_NODE_STREAMEDBUFFERUIVEC3,\
                                          NGL_NODE_STREAMEDBUFFERUIVEC4,\
                                          NGL_NODE_STREAMEDBUFFERFLOAT, \
                                          NGL_NODE_STREAMEDBUFFERVEC2,  \
                                          NGL_NODE_STREAMEDBUFFERVEC3,  \
                                          NGL_NODE_STREAMEDBUFFERVEC4,  \
                                          NGL_NODE_UNIFORMFLOAT,    \
                                          NGL_NODE_UNIFORMVEC2,     \
                                          NGL_NODE_UNIFORMVEC3,     \
                                          NGL_NODE_UNIFORMVEC4,     \
                                          NGL_NODE_UNIFORMQUAT,     \
                                          NGL_NODE_UNIFORMINT,      \
                                          NGL_NODE_UNIFORMIVEC2,    \
                                          NGL_NODE_UNIFORMIVEC3,    \
                                          NGL_NODE_UNIFORMIVEC4,    \
                                          NGL_NODE_UNIFORMUINT,     \
                                          NGL_NODE_UNIFORMUIVEC2,   \
                                          NGL_NODE_UNIFORMUIVEC3,   \
                                          NGL_NODE_UNIFORMUIVEC4,   \
                                          NGL_NODE_UNIFORMMAT4,     \
                                          NGL_NODE_ANIMATEDFLOAT,   \
                                          NGL_NODE_ANIMATEDVEC2,    \
                                          NGL_NODE_ANIMATEDVEC3,    \
                                          NGL_NODE_ANIMATEDVEC4,    \
                                          NGL_NODE_ANIMATEDQUAT,    \
                                          NGL_NODE_STREAMEDINT,     \
                                          NGL_NODE_STREAMEDIVEC2,   \
                                          NGL_NODE_STREAMEDIVEC3,   \
                                          NGL_NODE_STREAMEDIVEC4,   \
                                          NGL_NODE_STREAMEDUINT,    \
                                          NGL_NODE_STREAMEDUIVEC2,  \
                                          NGL_NODE_STREAMEDUIVEC3,  \
                                          NGL_NODE_STREAMEDUIVEC4,  \
                                          NGL_NODE_STREAMEDFLOAT,   \
                                          NGL_NODE_STREAMEDVEC2,    \
                                          NGL_NODE_STREAMEDVEC3,    \
                                          NGL_NODE_STREAMEDVEC4,    \
                                          NGL_NODE_STREAMEDMAT4,    \
                                          -1}

#define ATTRIBUTES_TYPES_LIST (const int[]){NGL_NODE_BUFFERFLOAT,   \
                                            NGL_NODE_BUFFERVEC2,    \
                                            NGL_NODE_BUFFERVEC3,    \
                                            NGL_NODE_BUFFERVEC4,    \
                                            NGL_NODE_BUFFERMAT4,    \
                                            -1}

#define GEOMETRY_TYPES_LIST (const int[]){NGL_NODE_CIRCLE,          \
                                          NGL_NODE_GEOMETRY,        \
                                          NGL_NODE_QUAD,            \
                                          NGL_NODE_TRIANGLE,        \
                                          -1}

#define OFFSET(x) offsetof(struct render_priv, x)
static const struct node_param render_params[] = {
    {"geometry", PARAM_TYPE_NODE, OFFSET(geometry), .flags=PARAM_FLAG_CONSTRUCTOR,
                 .node_types=GEOMETRY_TYPES_LIST,
                 .desc=NGLI_DOCSTRING("geometry to be rasterized")},
    {"program",  PARAM_TYPE_NODE, OFFSET(program),
                 .node_types=PROGRAMS_TYPES_LIST,
                 .desc=NGLI_DOCSTRING("program to be executed")},
    {"vertex_resources", PARAM_TYPE_NODEDICT, OFFSET(vertex_resources),
                         .node_types=INPUT_TYPES_LIST,
                         .desc=NGLI_DOCSTRING("resources made accessible to the vertex stage of the `program`")},
    {"fragment_resources", PARAM_TYPE_NODEDICT, OFFSET(fragment_resources),
                           .node_types=INPUT_TYPES_LIST,
                           .desc=NGLI_DOCSTRING("resources made accessible to the fragment stage of the `program`")},
    {"attributes", PARAM_TYPE_NODEDICT, OFFSET(attributes),
                 .node_types=ATTRIBUTES_TYPES_LIST,
                 .desc=NGLI_DOCSTRING("extra vertex attributes made accessible to the `program`")},
    {"instance_attributes", PARAM_TYPE_NODEDICT, OFFSET(instance_attributes),
                 .node_types=ATTRIBUTES_TYPES_LIST,
                 .desc=NGLI_DOCSTRING("per instance extra vertex attributes made accessible to the `program`")},
    {"nb_instances", PARAM_TYPE_INT, OFFSET(nb_instances),
                 .desc=NGLI_DOCSTRING("number of instances to draw")},
    {NULL}
};

static const char default_vertex_shader_tex[] =
    "void main()"                                                                       "\n"
    "{"                                                                                 "\n"
    "    ngl_out_pos = ngl_projection_matrix * ngl_modelview_matrix * ngl_position;"    "\n"
    "    var_uvcoord = ngl_uvcoord;"                                                    "\n"
    "    var_normal = ngl_normal_matrix * ngl_normal;"                                  "\n"
    "    var_tex0_coord = (tex0_coord_matrix * vec4(ngl_uvcoord, 0.0, 1.0)).xy;"        "\n"
    "}";

static const char default_vertex_shader_notex[] =
    "void main()"                                                                       "\n"
    "{"                                                                                 "\n"
    "    ngl_out_pos = ngl_projection_matrix * ngl_modelview_matrix * ngl_position;"    "\n"
    "    var_uvcoord = ngl_uvcoord;"                                                    "\n"
    "    var_normal = ngl_normal_matrix * ngl_normal;"                                  "\n"
    "}";

static const char default_fragment_shader[] =
    "void main()"                                                                       "\n"
    "{"                                                                                 "\n"
    "    ngl_out_color = ngl_texvideo(tex0, var_tex0_coord);"                           "\n"
    "}";

static const struct pgcraft_named_iovar vert2frag_vars[] = {
    {.name = "var_uvcoord",    .type = NGLI_TYPE_VEC2},
    {.name = "var_normal",     .type = NGLI_TYPE_VEC3},
    {.name = "var_tex0_coord", .type = NGLI_TYPE_VEC2},
};

static int has_tex(const struct hmap *resources)
{
    if (!resources)
        return 0;

    const struct hmap_entry *entry = NULL;
    while ((entry = ngli_hmap_next(resources, entry))) {
        const struct ngl_node *node = entry->data;
        if (node->class->category == NGLI_NODE_CATEGORY_TEXTURE)
            return 1;
    }
    return 0;
}

static int render_init(struct ngl_node *node)
{
    struct ngl_ctx *ctx = node->ctx;
    struct render_priv *s = node->priv_data;
    const struct program_priv *program = s->program ? s->program->priv_data : NULL;

    const char *vert_base = program && program->vertex   ? program->vertex : NULL;
    const char *frag_base = program && program->fragment ? program->fragment : NULL;

    ngli_darray_init(&s->vert2frag_vars, sizeof(struct pgcraft_named_iovar), 0);

    if (!vert_base || !frag_base) {
        const int has_tex0 = has_tex(s->fragment_resources);

        if (!ngli_darray_push(&s->vert2frag_vars, &vert2frag_vars[0]) ||
            !ngli_darray_push(&s->vert2frag_vars, &vert2frag_vars[1]) ||
            (has_tex0 && !ngli_darray_push(&s->vert2frag_vars, &vert2frag_vars[2])))
            return NGL_ERROR_MEMORY;

        if (!vert_base)
            vert_base = has_tex0 ? default_vertex_shader_tex
                                 : default_vertex_shader_notex;

        if (!frag_base)
            frag_base = default_fragment_shader;
    }

    if (program && program->vert2frag_vars) {
        const struct hmap_entry *e = NULL;
        while ((e = ngli_hmap_next(program->vert2frag_vars, e))) {
            const struct ngl_node *iovar_node = e->data;
            const struct iovariable_priv *iovar_priv = iovar_node->priv_data;
            struct pgcraft_named_iovar *iovar = ngli_darray_push(&s->vert2frag_vars, NULL);
            if (!iovar)
                return NGL_ERROR_MEMORY;
            snprintf(iovar->name, sizeof(iovar->name), "%s", e->key);
            iovar->type = iovar_priv->type;
        }
    }

    struct pass_params params = {
        .label = node->label,
        .geometry = s->geometry,
        .vert_base = vert_base,
        .frag_base = frag_base,
        .vertex_resources = s->vertex_resources,
        .fragment_resources = s->fragment_resources,
        .properties = program ? program->properties : NULL,
        .attributes = s->attributes,
        .instance_attributes = s->instance_attributes,
        .nb_instances = s->nb_instances,
        .vert2frag_vars = ngli_darray_data(&s->vert2frag_vars),
        .nb_vert2frag_vars = ngli_darray_count(&s->vert2frag_vars),
        .nb_frag_output = program && program->nb_frag_output ? program->nb_frag_output : 0,
    };
    return ngli_pass_init(&s->pass, ctx, &params);
}

static int render_prepare(struct ngl_node *node)
{
    struct render_priv *s = node->priv_data;
    return ngli_pass_prepare(&s->pass);
}

static void render_uninit(struct ngl_node *node)
{
    struct render_priv *s = node->priv_data;
    ngli_pass_uninit(&s->pass);
    ngli_darray_reset(&s->vert2frag_vars);
}

static int render_update(struct ngl_node *node, double t)
{
    struct render_priv *s = node->priv_data;
    return ngli_pass_update(&s->pass, t);
}

static void render_draw(struct ngl_node *node)
{
    struct render_priv *s = node->priv_data;
    ngli_pass_exec(&s->pass);
}

const struct node_class ngli_render_class = {
    .id        = NGL_NODE_RENDER,
    .name      = "Render",
    .init      = render_init,
    .prepare   = render_prepare,
    .uninit    = render_uninit,
    .update    = render_update,
    .draw      = render_draw,
    .priv_size = sizeof(struct render_priv),
    .params    = render_params,
    .file      = __FILE__,
};

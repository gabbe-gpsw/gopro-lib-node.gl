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

#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "nodegl.h"
#include "nodes.h"
#include "log.h"

#define OFFSET(x) offsetof(struct hud, x)
static const struct node_param hud_params[] = {
    {"child", PARAM_TYPE_NODE, OFFSET(child), .flags=PARAM_FLAG_CONSTRUCTOR,
              .desc=NGLI_DOCSTRING("scene to benchmark")},
    {"measure_window", PARAM_TYPE_INT, OFFSET(measure_window), {.i64=60},
                       .desc=NGLI_DOCSTRING("window size for latency measures")},
    {"refresh_rate",   PARAM_TYPE_RATIONAL, OFFSET(refresh_rate),
                       .desc=NGLI_DOCSTRING("refresh data buffer every `update_rate` second")},
    {"export_filename", PARAM_TYPE_STR, OFFSET(export_filename),
                        .desc=NGLI_DOCSTRING("path to export file (CSV)")},
    {"bg_color", PARAM_TYPE_VEC4, OFFSET(bg_color), {.vec={0.0, 0.0, 0.0, 1.0}},
                 .desc=NGLI_DOCSTRING("background buffer color")},
    {"aspect_ratio", PARAM_TYPE_RATIONAL, OFFSET(aspect_ratio),
                     .desc=NGLI_DOCSTRING("buffer aspect ratio")},
    {NULL}
};

#define FONT_H 8
#define FONT_W 8
#define DATA_NBCHAR_W 20
#define DATA_GRAPH_W 320

static const uint8_t font8[128][8] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00},
    {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00},
    {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00},
    {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00},
    {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00},
    {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00},
    {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00},
    {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00},
    {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00},
    {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00},
    {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00},
    {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00},
    {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00},
    {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00},
    {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00},
    {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00},
    {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00},
    {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00},
    {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00},
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00},
    {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06},
    {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00},
    {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00},
    {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00},
    {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00},
    {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00},
    {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00},
    {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00},
    {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00},
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00},
    {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00},
    {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00},
    {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00},
    {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00},
    {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00},
    {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00},
    {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00},
    {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00},
    {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00},
    {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00},
    {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00},
    {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00},
    {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00},
    {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00},
    {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00},
    {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00},
    {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00},
    {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00},
    {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00},
    {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00},
    {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF},
    {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00},
    {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00},
    {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00},
    {0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00},
    {0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00},
    {0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00},
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00},
    {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E},
    {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00},
    {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00},
    {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00},
    {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00},
    {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00},
    {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F},
    {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78},
    {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00},
    {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00},
    {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00},
    {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00},
    {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00},
    {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F},
    {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00},
    {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00},
    {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00},
    {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00},
    {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
};

#define WIDGET_PADDING 4
#define WIDGET_MARGIN  2

enum {
    LATENCY_UPDATE_CPU,
    LATENCY_UPDATE_GPU,
    LATENCY_DRAW_CPU,
    LATENCY_DRAW_GPU,
    LATENCY_TOTAL_CPU,
    LATENCY_TOTAL_GPU,
    NB_LATENCY
};

static const struct {
    const char *label;
    const uint32_t color;
    char unit;
} latency_specs[] = {
    [LATENCY_UPDATE_CPU] = {"update CPU", 0xF43DF4FF, 'u'},
    [LATENCY_UPDATE_GPU] = {"update GPU", 0x3D3DF4FF, 'n'},
    [LATENCY_DRAW_CPU]   = {"draw   CPU", 0x3DF4F4FF, 'u'},
    [LATENCY_DRAW_GPU]   = {"draw   GPU", 0x3DF43DFF, 'n'},
    [LATENCY_TOTAL_CPU]  = {"total  CPU", 0xF4F43DFF, 'u'},
    [LATENCY_TOTAL_GPU]  = {"total  GPU", 0xF43D3DFF, 'n'},
};

NGLI_STATIC_ASSERT(hud_nb_latency, NGLI_ARRAY_NB(latency_specs) == NB_LATENCY);

enum widget_type {
    WIDGET_LATENCY,
};

struct data_graph {
    int64_t *values;
    int nb_values;
    int count;
    int pos;
    int64_t min;
    int64_t max;
};

struct latency_measure {
    int64_t *times;
    int count;
    int pos;
    int64_t total_times;
};

struct widget_latency {
    struct latency_measure measures[NB_LATENCY];

    GLuint query;
    void (*glGenQueries)(const struct glcontext *gl, GLsizei n, GLuint * ids);
    void (*glDeleteQueries)(const struct glcontext *gl, GLsizei n, const GLuint * ids);
    void (*glBeginQuery)(const struct glcontext *gl, GLenum target, GLuint id);
    void (*glEndQuery)(const struct glcontext *gl, GLenum target);
    void (*glGetQueryObjectui64v)(const struct glcontext *gl, GLuint id, GLenum pname, GLuint64 *params);
};

struct rect {
    int x, y, w, h;
};

struct widget {
    enum widget_type type;
    struct rect rect;
    int text_x, text_y;
    struct rect graph_rect;
    struct data_graph *data_graph;
    const void *user_data;
    void *priv_data;
};

struct widget_spec {
    int text_cols, text_rows;
    int graph_w, graph_h;
    int nb_data_graph;
    size_t priv_size;
    int (*init)(struct ngl_node *node, struct widget *widget);
    void (*make_stats)(struct ngl_node *node, struct widget *widget);
    void (*draw)(struct ngl_node *node, struct widget *widget);
    void (*csv_header)(struct ngl_node *node, struct widget *widget, struct bstr *dst);
    void (*csv_report)(struct ngl_node *node, struct widget *widget, struct bstr *dst);
    void (*uninit)(struct ngl_node *node, struct widget *widget);
};

static void noop(const struct glcontext *gl, ...)
{
}

static int widget_latency_init(struct ngl_node *node, struct widget *widget)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;

    struct hud *s = node->priv_data;
    struct widget_latency *priv = widget->priv_data;

    if (gl->features & NGLI_FEATURE_TIMER_QUERY) {
        priv->glGenQueries          = ngli_glGenQueries;
        priv->glDeleteQueries       = ngli_glDeleteQueries;
        priv->glBeginQuery          = ngli_glBeginQuery;
        priv->glEndQuery            = ngli_glEndQuery;
        priv->glGetQueryObjectui64v = ngli_glGetQueryObjectui64v;
    } else if (gl->features & NGLI_FEATURE_EXT_DISJOINT_TIMER_QUERY) {
        priv->glGenQueries          = ngli_glGenQueriesEXT;
        priv->glDeleteQueries       = ngli_glDeleteQueriesEXT;
        priv->glBeginQuery          = ngli_glBeginQueryEXT;
        priv->glEndQuery            = ngli_glEndQueryEXT;
        priv->glGetQueryObjectui64v = ngli_glGetQueryObjectui64vEXT;
    } else {
        priv->glGenQueries          = (void *)noop;
        priv->glDeleteQueries       = (void *)noop;
        priv->glBeginQuery          = (void *)noop;
        priv->glEndQuery            = (void *)noop;
        priv->glGetQueryObjectui64v = (void *)noop;
    }

    priv->glGenQueries(gl, 1, &priv->query);

    ngli_assert(NB_LATENCY == NGLI_ARRAY_NB(priv->measures));

    s->measure_window = NGLI_MAX(s->measure_window, 1);
    for (int i = 0; i < NB_LATENCY; i++) {
        int64_t *times = calloc(s->measure_window, sizeof(*times));
        if (!times)
            return -1;
        priv->measures[i].times = times;
    }

    return 0;
}

static void register_time(struct hud *s, struct latency_measure *m, int64_t t)
{
    m->total_times = m->total_times - m->times[m->pos] + t;
    m->times[m->pos] = t;
    m->pos = (m->pos + 1) % s->measure_window;
    m->count = NGLI_MIN(m->count + 1, s->measure_window);
}

static int widget_latency_update(struct ngl_node *node, struct widget *widget, double t)
{
    int ret;
    struct hud *s = node->priv_data;
    struct ngl_node *child = s->child;

    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct widget_latency *priv = widget->priv_data;

    int timer_active = ctx->timer_active;
    if (timer_active) {
        LOG(WARNING, "GPU timings will not be available when using multiple HUD "
                     "in the same graph due to GL limitations");
    } else {
        ctx->timer_active = 1;
        priv->glBeginQuery(gl, GL_TIME_ELAPSED, priv->query);
    }

    int64_t update_start = ngli_gettime();
    ret = ngli_node_update(child, t);
    int64_t update_end = ngli_gettime();

    GLuint64 gpu_tupdate = 0;
    if (!timer_active) {
        priv->glEndQuery(gl, GL_TIME_ELAPSED);
        priv->glGetQueryObjectui64v(gl, priv->query, GL_QUERY_RESULT, &gpu_tupdate);
        ctx->timer_active = 0;
    }

    register_time(s, &priv->measures[LATENCY_UPDATE_CPU], update_end - update_start);
    register_time(s, &priv->measures[LATENCY_UPDATE_GPU], gpu_tupdate);

    return ret;
}

static void widget_latency_make_stats(struct ngl_node *node, struct widget *widget)
{
    struct hud *s = node->priv_data;

    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct widget_latency *priv = widget->priv_data;

    int timer_active = ctx->timer_active;
    if (!timer_active) {
        ctx->timer_active = 1;
        priv->glBeginQuery(gl, GL_TIME_ELAPSED, priv->query);
    }

    const int64_t draw_start = ngli_gettime();
    ngli_node_draw(s->child);
    const int64_t draw_end = ngli_gettime();

    GLuint64 gpu_tdraw = 0;
    if (!timer_active) {
        priv->glEndQuery(gl, GL_TIME_ELAPSED);
        priv->glGetQueryObjectui64v(gl, priv->query, GL_QUERY_RESULT, &gpu_tdraw);
        ctx->timer_active = 0;
    }

    int64_t cpu_tdraw = draw_end - draw_start;
    register_time(s, &priv->measures[LATENCY_DRAW_CPU], cpu_tdraw);
    register_time(s, &priv->measures[LATENCY_DRAW_GPU], gpu_tdraw);

    const struct latency_measure *cpu_up = &priv->measures[LATENCY_UPDATE_CPU];
    const struct latency_measure *gpu_up = &priv->measures[LATENCY_UPDATE_GPU];
    const int last_cpu_up_pos = (cpu_up->pos ? cpu_up->pos : s->measure_window) - 1;
    const int last_gpu_up_pos = (gpu_up->pos ? gpu_up->pos : s->measure_window) - 1;
    const int64_t cpu_tupdate = cpu_up->times[last_cpu_up_pos];
    const int64_t gpu_tupdate = gpu_up->times[last_gpu_up_pos];
    register_time(s, &priv->measures[LATENCY_TOTAL_CPU], cpu_tdraw + cpu_tupdate);
    register_time(s, &priv->measures[LATENCY_TOTAL_GPU], gpu_tdraw + gpu_tupdate);
}

static inline uint8_t *set_color(uint8_t *p, uint32_t rgba)
{
    p[0] = rgba >> 24;
    p[1] = rgba >> 16 & 0xff;
    p[2] = rgba >>  8 & 0xff;
    p[3] = rgba       & 0xff;
    return p + 4;
}

static inline int get_pixel_pos(struct hud *s, int px, int py)
{
    return (py * s->data_w + px) * 4;
}

static int clip(int x, int min, int max)
{
    if (x < min)
        return min;
    if (x > max)
        return max;
    return x;
}

static void draw_line_graph(struct hud *s,
                            const struct data_graph *d,
                            const struct rect *rect,
                            int64_t graph_min, int64_t graph_max,
                            const uint32_t c)
{
    const int64_t graph_h = graph_max - graph_min;
    const float vscale = (float)rect->h / graph_h;
    const int start = (d->pos - d->count + d->nb_values) % d->nb_values;
    int prev_y;

    for (int k = 0; k < d->count; k++) {
        const int64_t v = d->values[(start + k) % d->nb_values];
        const int h = (v - graph_min) * vscale;
        const int y = clip(rect->h - 1 - h, 0, rect->h - 1);
        uint8_t *p = s->data_buf + get_pixel_pos(s, rect->x + k, rect->y + y);

        set_color(p, c);
        if (k) {
            const int sign = prev_y < y ? 1 : -1;
            const int column_h = abs(prev_y - y);
            uint8_t *p = s->data_buf + get_pixel_pos(s, rect->x + k, rect->y + prev_y);
            for (int z = 0; z < column_h; z++) {
                set_color(p, c);
                p += sign * s->data_w * 4;
            }
        }
        prev_y = y;
    }
}

static void print_text(struct hud *s, int x, int y, const char *buf, const uint32_t c)
{
    uint8_t *start = s->data_buf + get_pixel_pos(s, x, y);
    for (int i = 0; buf[i]; i++) {
        uint8_t *p = start + i * FONT_W * 4;
        for (int char_y = 0; char_y < FONT_H; char_y++) {
            for (int m = 0; m < FONT_W; m++)
                p = (font8[buf[i] & 0x7f][char_y] & (1<<m)) ? set_color(p, c) : p + 4;
            p += (s->data_w - 8) * 4;
        }
    }
}

static void widgets_clear(struct hud *s)
{
    struct darray *widgets_array = &s->widgets;
    struct widget *widgets = ngli_darray_data(widgets_array);
    for (int i = 0; i < ngli_darray_count(widgets_array); i++) {
        const struct widget *widget = &widgets[i];
        const struct rect *rect = &widget->rect;
        for (int y = 0; y < rect->h; y++) {
            for (int x = 0; x < rect->w; x++) {
                const int pos = get_pixel_pos(s, rect->x + x, rect->y + y);
                set_color(s->data_buf + pos, s->bg_color_u32);
            }
        }
    }
}

static void register_graph_value(struct data_graph *d, int64_t v)
{
    const int64_t old_v = d->values[d->pos];

    d->values[d->pos] = v;
    d->pos = (d->pos + 1) % d->nb_values;
    d->count = NGLI_MIN(d->count + 1, d->nb_values);

    /* update min */
    if (old_v == d->min) {
        d->min = d->values[0];
        for (int i = 1; i < d->nb_values; i++)
            d->min = NGLI_MIN(d->min, d->values[i]);
    } else if (v < d->min) {
        d->min = v;
    }

    /* update max */
    if (old_v == d->max) {
        d->max = d->values[0];
        for (int i = 1; i < d->nb_values; i++)
            d->max = NGLI_MAX(d->max, d->values[i]);
    } else if (v > d->max) {
        d->max = v;
    }
}

static int64_t get_latency_avg(const struct widget_latency *priv, int id)
{
    const struct latency_measure *m = &priv->measures[id];
    return m->total_times / m->count / (latency_specs[id].unit == 'u' ? 1 : 1000);
}

static void widget_latency_draw(struct ngl_node *node, struct widget *widget)
{
    struct hud *s = node->priv_data;
    struct widget_latency *priv = widget->priv_data;

    char buf[DATA_NBCHAR_W + 1];
    for (int i = 0; i < NB_LATENCY; i++) {
        const int64_t t = get_latency_avg(priv, i);

        snprintf(buf, sizeof(buf), "%s %5" PRId64 "usec", latency_specs[i].label, t);
        print_text(s, widget->text_x, widget->text_y + i * FONT_H, buf, latency_specs[i].color);
        register_graph_value(&widget->data_graph[i], t);
    }

    int64_t graph_min = widget->data_graph[0].min;
    int64_t graph_max = widget->data_graph[0].max;
    for (int i = 1; i < NB_LATENCY; i++) {
        graph_min = NGLI_MIN(graph_min, widget->data_graph[i].min);
        graph_max = NGLI_MAX(graph_max, widget->data_graph[i].max);
    }

    const int64_t graph_h = graph_max - graph_min;
    if (graph_h) {
        for (int i = 0; i < NB_LATENCY; i++)
            draw_line_graph(s, &widget->data_graph[i], &widget->graph_rect,
                            graph_min, graph_max, latency_specs[i].color);
    }
}

static void widget_latency_csv_header(struct ngl_node *node, struct widget *widget, struct bstr *dst)
{
    for (int i = 0; i < NB_LATENCY; i++)
        ngli_bstr_print(dst, "%s%s", i ? "," : "", latency_specs[i].label);
}

static void widget_latency_csv_report(struct ngl_node *node, struct widget *widget, struct bstr *dst)
{
    const struct widget_latency *priv = widget->priv_data;

    for (int i = 0; i < NB_LATENCY; i++) {
        const int64_t t = get_latency_avg(priv, i);
        ngli_bstr_print(dst, "%s%"PRId64, i ? "," : "", t);
    }
}

static void widget_latency_uninit(struct ngl_node *node, struct widget *widget)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct widget_latency *priv = widget->priv_data;

    for (int i = 0; i < NB_LATENCY; i++)
        free(priv->measures[i].times);
    priv->glDeleteQueries(gl, 1, &priv->query);
}

static const struct widget_spec widget_specs[] = {
    [WIDGET_LATENCY] = {
        .text_cols     = DATA_NBCHAR_W,
        .text_rows     = NB_LATENCY,
        .graph_w       = DATA_GRAPH_W,
        .nb_data_graph = NB_LATENCY,
        .priv_size     = sizeof(struct widget_latency),
        .init          = widget_latency_init,
        .make_stats    = widget_latency_make_stats,
        .draw          = widget_latency_draw,
        .csv_header    = widget_latency_csv_header,
        .csv_report    = widget_latency_csv_report,
        .uninit        = widget_latency_uninit,
    },
};

static inline int get_widget_width(enum widget_type type)
{
    const struct widget_spec *spec = &widget_specs[type];
    const int horizontal_layout = !spec->graph_h;
    return spec->graph_w
         + spec->text_cols * FONT_W
         + WIDGET_PADDING * (2 + horizontal_layout);
}

static inline int get_widget_height(enum widget_type type)
{
    const struct widget_spec *spec = &widget_specs[type];
    const int vertical_layout = !!spec->graph_h;
    return spec->graph_h
         + spec->text_rows * FONT_H
         + WIDGET_PADDING * (2 + vertical_layout);
}

static int create_widget(struct hud *s, enum widget_type type, const void *user_data, int x, int y)
{
    if (x < 0)
        x = s->data_w + x;
    if (y < 0)
        y = s->data_h + y;

    const struct widget_spec *spec = &widget_specs[type];

    ngli_assert(spec->text_cols && spec->text_rows);
    ngli_assert(spec->graph_w ^ spec->graph_h);
    ngli_assert(spec->nb_data_graph);

    const int horizontal_layout = !spec->graph_h;
    struct widget widget = {
        .type      = type,
        .rect.x    = x,
        .rect.y    = y,
        .rect.w    = get_widget_width(type),
        .rect.h    = get_widget_height(type),
        .text_x    = x + WIDGET_PADDING,
        .text_y    = y + WIDGET_PADDING,
        .user_data = user_data,
    };

    if (horizontal_layout) {
        widget.graph_rect.x = x + spec->text_cols * FONT_W + WIDGET_PADDING * 2;
        widget.graph_rect.y = y + WIDGET_PADDING;
        widget.graph_rect.w = spec->graph_w;
        widget.graph_rect.h = widget.rect.h - WIDGET_PADDING * 2;
    } else {
        widget.graph_rect.x = x + WIDGET_PADDING;
        widget.graph_rect.y = y + spec->text_rows * FONT_H + WIDGET_PADDING * 2;
        widget.graph_rect.w = widget.rect.w - WIDGET_PADDING * 2;
        widget.graph_rect.h = spec->graph_h;
    }

    struct widget *widgetp = ngli_darray_push(&s->widgets, &widget);
    if (!widgetp)
        return -1;

    widgetp->priv_data = calloc(1, spec->priv_size);
    if (!widgetp->priv_data)
        return -1;

    widgetp->data_graph = calloc(spec->nb_data_graph, sizeof(*widgetp->data_graph));
    if (!widgetp->data_graph)
        return -1;
    for (int i = 0; i < spec->nb_data_graph; i++) {
        struct data_graph *d = &widgetp->data_graph[i];
        d->nb_values = widgetp->graph_rect.w;
        d->values = calloc(d->nb_values, sizeof(*d->values));
        if (!d->values)
            return -1;
    }

    return 0;
}

static int widgets_init(struct ngl_node *node)
{
    struct hud *s = node->priv_data;

    ngli_darray_init(&s->widgets, sizeof(struct widget), 0);

    /* Smallest dimensions possible (in pixels) */
    const int min_width  = WIDGET_MARGIN * 2
                         + get_widget_width(WIDGET_LATENCY);
    const int min_height = WIDGET_MARGIN * 2
                         + get_widget_height(WIDGET_LATENCY);

    /* Compute buffer dimensions according to user specified aspect ratio and
     * minimal dimensions */
    const int *ar = s->aspect_ratio;
    s->data_w = min_width;
    s->data_h = min_width * ar[1] / ar[0];
    if (s->data_h < min_height) {
        s->data_w = min_height * ar[0] / ar[1];
        s->data_h = min_height;
    }

    /* Latency widget in the top-left */
    const int x_latency = WIDGET_MARGIN;
    const int y_latency = WIDGET_MARGIN;
    int ret = create_widget(s, WIDGET_LATENCY, NULL, x_latency, y_latency);
    if (ret < 0)
        return ret;

    /* Call init on every widget */
    struct darray *widgets_array = &s->widgets;
    struct widget *widgets = ngli_darray_data(widgets_array);
    for (int i = 0; i < ngli_darray_count(widgets_array); i++) {
        struct widget *widget = &widgets[i];
        int ret = widget_specs[widget->type].init(node, widget);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static void widgets_make_stats(struct ngl_node *node)
{
    struct hud *s = node->priv_data;
    struct darray *widgets_array = &s->widgets;
    struct widget *widgets = ngli_darray_data(widgets_array);
    for (int i = 0; i < ngli_darray_count(widgets_array); i++) {
        struct widget *widget = &widgets[i];
        widget_specs[widget->type].make_stats(node, widget);
    }
}

static void widgets_draw(struct ngl_node *node)
{
    struct hud *s = node->priv_data;
    struct darray *widgets_array = &s->widgets;
    struct widget *widgets = ngli_darray_data(widgets_array);
    for (int i = 0; i < ngli_darray_count(widgets_array); i++) {
        struct widget *widget = &widgets[i];
        widget_specs[widget->type].draw(node, widget);
    }
}

static void widgets_csv_header(struct ngl_node *node)
{
    struct hud *s = node->priv_data;
    struct darray *widgets_array = &s->widgets;
    struct widget *widgets = ngli_darray_data(widgets_array);
    for (int i = 0; i < ngli_darray_count(widgets_array); i++) {
        struct widget *widget = &widgets[i];
        ngli_bstr_print(s->csv_line, i ? "," : "");
        widget_specs[widget->type].csv_header(node, widget, s->csv_line);
    }
}

static void widgets_csv_report(struct ngl_node *node)
{
    struct hud *s = node->priv_data;
    struct darray *widgets_array = &s->widgets;
    struct widget *widgets = ngli_darray_data(widgets_array);
    for (int i = 0; i < ngli_darray_count(widgets_array); i++) {
        ngli_bstr_print(s->csv_line, ",");
        struct widget *widget = &widgets[i];
        widget_specs[widget->type].csv_report(node, widget, s->csv_line);
    }
}

static void free_widget(struct widget *widget)
{
    free(widget->priv_data);
    for (int i = 0; i < widget_specs[widget->type].nb_data_graph; i++)
        free(widget->data_graph[i].values);
    free(widget->data_graph);
}

static void widgets_uninit(struct ngl_node *node)
{
    struct hud *s = node->priv_data;
    struct darray *widgets_array = &s->widgets;
    struct widget *widgets = ngli_darray_data(widgets_array);
    for (int i = 0; i < ngli_darray_count(widgets_array); i++) {
        struct widget *widget = &widgets[i];
        widget_specs[widget->type].uninit(node, widget);
        free_widget(widget);
    }
    ngli_darray_reset(&s->widgets);
}

static int hud_init(struct ngl_node *node)
{
    struct hud *s = node->priv_data;

    s->bg_color_u32 = ((unsigned)(s->bg_color[0] * 255) & 0xff) << 24 |
                      ((unsigned)(s->bg_color[1] * 255) & 0xff) << 16 |
                      ((unsigned)(s->bg_color[2] * 255) & 0xff) <<  8 |
                      ((unsigned)(s->bg_color[3] * 255) & 0xff);

    int ret = widgets_init(node);
    if (ret < 0)
        return ret;

    s->data_buf = calloc(s->data_w * s->data_h, 4);
    if (!s->data_buf)
        return -1;

    widgets_clear(s);

    if (s->refresh_rate[1])
        s->refresh_rate_interval = s->refresh_rate[0] / (double)s->refresh_rate[1];
    s->last_refresh_time = -1;

    if (s->export_filename) {
        s->fd_export = open(s->export_filename, O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (s->fd_export == -1) {
            LOG(ERROR, "unable to open \"%s\" for writing", s->export_filename);
            return -1;
        }

        s->csv_line = ngli_bstr_create();
        if (!s->csv_line)
            return -1;

        widgets_csv_header(node);
        ngli_bstr_print(s->csv_line, "\n");

        const int len = ngli_bstr_len(s->csv_line);
        ssize_t n = write(s->fd_export, ngli_bstr_strptr(s->csv_line), len);
        if (n != len) {
            LOG(ERROR, "unable to write CSV header");
            return -1;
        }
    }

    return 0;
}

static int hud_update(struct ngl_node *node, double t)
{
    struct hud *s = node->priv_data;

    s->need_refresh = fabs(t - s->last_refresh_time) >= s->refresh_rate_interval;
    if (s->need_refresh)
        s->last_refresh_time = t;

    struct darray *widgets_array = &s->widgets;
    struct widget *widgets = ngli_darray_data(widgets_array);
    return widget_latency_update(node, &widgets[0], t);
}

static void hud_draw(struct ngl_node *node)
{
    struct hud *s = node->priv_data;

    widgets_make_stats(node);
    if (s->need_refresh) {
        widgets_clear(s);

        if (s->export_filename) {
            ngli_bstr_clear(s->csv_line);
            widgets_csv_report(node);
            ngli_bstr_print(s->csv_line, "\n");
            const int len = ngli_bstr_len(s->csv_line);
            write(s->fd_export, ngli_bstr_strptr(s->csv_line), len);
        }
        widgets_draw(node);
    }
}

static void hud_uninit(struct ngl_node *node)
{
    struct hud *s = node->priv_data;

    widgets_uninit(node);
    free(s->data_buf);
    if (s->export_filename) {
        close(s->fd_export);
        ngli_bstr_freep(&s->csv_line);
    }
}

const struct node_class ngli_hud_class = {
    .id        = NGL_NODE_HUD,
    .name      = "HUD",
    .init      = hud_init,
    .update    = hud_update,
    .draw      = hud_draw,
    .uninit    = hud_uninit,
    .priv_size = sizeof(struct hud),
    .params    = hud_params,
    .file      = __FILE__,
};

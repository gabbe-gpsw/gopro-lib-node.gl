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

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sxplayer.h>

#include "android_surface.h"
#include "glincludes.h"
#include "hwupload.h"
#include "log.h"
#include "math_utils.h"
#include "nodegl.h"
#include "nodes.h"

static int common_get_data_format(int pix_fmt)
{
    switch (pix_fmt) {
    case SXPLAYER_PIXFMT_RGBA:
        return NGLI_FORMAT_R8G8B8A8_UNORM;
    case SXPLAYER_PIXFMT_BGRA:
        return NGLI_FORMAT_B8G8R8A8_UNORM;
    case SXPLAYER_SMPFMT_FLT:
        return NGLI_FORMAT_R32_SFLOAT;
    default:
        return -1;
    }
}

static int common_init(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct texture *s = node->priv_data;

    s->data_format = common_get_data_format(frame->pix_fmt);
    if (s->data_format < 0)
        return -1;

    int ret = ngli_format_get_gl_format_type(gl,
                                             s->data_format,
                                             &s->format,
                                             &s->internal_format,
                                             &s->type);
    if (ret < 0)
        return ret;

    return 0;
}

static int common_map_frame(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct texture *s = node->priv_data;

    const int linesize       = frame->linesize >> 2;
    s->coordinates_matrix[0] = linesize ? frame->width / (float)linesize : 1.0;

    ngli_texture_update_local_texture(node, linesize, frame->height, 0, frame->data);

    return 0;
}

static const struct hwmap_class hwmap_common_class = {
    .name      = "default",
    .init      = common_init,
    .map_frame = common_map_frame,
};

static const struct hwmap_class *common_get_hwmap(struct ngl_node *node, struct sxplayer_frame *frame)
{
    return &hwmap_common_class;
}

const struct hwupload_class ngli_hwupload_common_class = {
    .get_hwmap = common_get_hwmap,
};
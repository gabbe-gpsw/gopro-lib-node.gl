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

#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>
#include <OpenGL/CGLIOSurface.h>

#include "format.h"
#include "glincludes.h"
#include "hwupload.h"
#include "image.h"
#include "log.h"
#include "math_utils.h"
#include "nodegl.h"
#include "nodes.h"

struct hwupload_vt_darwin {
    struct sxplayer_frame *frame;
    struct texture planes[2];
};

static int vt_darwin_map_frame(struct ngl_node *node, struct sxplayer_frame *frame)
{
    struct ngl_ctx *ctx = node->ctx;
    struct glcontext *gl = ctx->glcontext;
    struct texture_priv *s = node->priv_data;
    struct hwupload *hwupload = &s->hwupload;
    struct hwupload_vt_darwin *vt = hwupload->hwmap_priv_data;

    sxplayer_release_frame(vt->frame);
    vt->frame = frame;

    CVPixelBufferRef cvpixbuf = (CVPixelBufferRef)frame->data;
    IOSurfaceRef surface = CVPixelBufferGetIOSurface(cvpixbuf);
    if (!surface) {
        LOG(ERROR, "could not get IOSurface from buffer");
        return -1;
    }

    OSType format = IOSurfaceGetPixelFormat(surface);
    if (format != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
        LOG(ERROR, "unsupported IOSurface format: 0x%x", format);
        return -1;
    }

    for (int i = 0; i < 2; i++) {
        struct texture *plane = &vt->planes[i];

        ngli_glBindTexture(gl, plane->target, plane->id);

        int width = IOSurfaceGetWidthOfPlane(surface, i);
        int height = IOSurfaceGetHeightOfPlane(surface, i);
        ngli_texture_set_dimensions(plane, width, height, 0);

        CGLError err = CGLTexImageIOSurface2D(CGLGetCurrentContext(), plane->target,
                                              plane->internal_format, width, height,
                                              plane->format, plane->format_type, surface, i);
        if (err != kCGLNoError) {
            LOG(ERROR, "could not bind IOSurface plane %d to texture %d: %d", i, s->texture.id, err);
            return -1;
        }

        ngli_glBindTexture(gl, GL_TEXTURE_RECTANGLE, 0);
    }

    return 0;
}

static int support_direct_rendering(struct ngl_node *node)
{
    struct texture_priv *s = node->priv_data;
    int direct_rendering = s->supported_image_layouts & (1 << NGLI_IMAGE_LAYOUT_NV12_RECTANGLE);

    if (direct_rendering && s->params.mipmap_filter) {
        LOG(WARNING, "IOSurface NV12 buffers do not support mipmapping: "
            "disabling direct rendering");
        direct_rendering = 0;
    }

    return direct_rendering;
}

static int vt_darwin_init(struct ngl_node *node, struct sxplayer_frame * frame)
{
    struct ngl_ctx *ctx = node->ctx;
    struct texture_priv *s = node->priv_data;
    struct hwupload *hwupload = &s->hwupload;
    struct hwupload_vt_darwin *vt = hwupload->hwmap_priv_data;

    for (int i = 0; i < 2; i++) {
        struct texture *plane = &vt->planes[i];
        struct texture_params plane_params = NGLI_TEXTURE_PARAM_DEFAULTS;
        plane_params.format = i == 0 ? NGLI_FORMAT_R8_UNORM : NGLI_FORMAT_R8G8_UNORM;
        plane_params.rectangle = 1;
        plane_params.external_storage = 1;

        int ret = ngli_texture_init(plane, ctx, &plane_params);
        if (ret < 0)
            return ret;
    }

    struct image_params image_params = {
        .width = frame->width,
        .height = frame->height,
        .layout = NGLI_IMAGE_LAYOUT_NV12_RECTANGLE,
        .color_info = ngli_color_info_from_sxplayer_frame(frame),
    };
    struct texture *planes[] = {
        &vt->planes[0],
        &vt->planes[1],
    };
    ngli_image_init(&hwupload->mapped_image, &image_params, planes);

    hwupload->require_hwconv = !support_direct_rendering(node);

    return 0;
}

static void vt_darwin_uninit(struct ngl_node *node)
{
    struct texture_priv *s = node->priv_data;
    struct hwupload *hwupload = &s->hwupload;
    struct hwupload_vt_darwin *vt = hwupload->hwmap_priv_data;

    for (int i = 0; i < 2; i++)
        ngli_texture_reset(&vt->planes[i]);

    sxplayer_release_frame(vt->frame);
    vt->frame = NULL;
}

const struct hwmap_class ngli_hwmap_vt_darwin_class = {
    .name      = "videotoolbox (iosurface → nv12)",
    .flags     = HWMAP_FLAG_FRAME_OWNER,
    .priv_size = sizeof(struct hwupload_vt_darwin),
    .init      = vt_darwin_init,
    .map_frame = vt_darwin_map_frame,
    .uninit    = vt_darwin_uninit,
};

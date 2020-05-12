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

#include "format.h"
#include "glcontext.h"
#include "nodes.h"

static const struct {
    int nb_comp;
    int size;
} format_comp_sizes[NGLI_FORMAT_NB] = {
    [NGLI_FORMAT_R8_UNORM]            = {1, 1},
    [NGLI_FORMAT_R8_SNORM]            = {1, 1},
    [NGLI_FORMAT_R8_UINT]             = {1, 1},
    [NGLI_FORMAT_R8_SINT]             = {1, 1},
    [NGLI_FORMAT_R8G8_UNORM]          = {2, 1 + 1},
    [NGLI_FORMAT_R8G8_SNORM]          = {2, 1 + 1},
    [NGLI_FORMAT_R8G8_UINT]           = {2, 1 + 1},
    [NGLI_FORMAT_R8G8_SINT]           = {2, 1 + 1},
    [NGLI_FORMAT_R8G8B8_UNORM]        = {3, 1 + 1 + 1},
    [NGLI_FORMAT_R8G8B8_SNORM]        = {3, 1 + 1 + 1},
    [NGLI_FORMAT_R8G8B8_UINT]         = {3, 1 + 1 + 1},
    [NGLI_FORMAT_R8G8B8_SINT]         = {3, 1 + 1 + 1},
    [NGLI_FORMAT_R8G8B8_SRGB]         = {3, 1 + 1 + 1},
    [NGLI_FORMAT_R8G8B8A8_UNORM]      = {4, 1 + 1 + 1 + 1},
    [NGLI_FORMAT_R8G8B8A8_SNORM]      = {4, 1 + 1 + 1 + 1},
    [NGLI_FORMAT_R8G8B8A8_UINT]       = {4, 1 + 1 + 1 + 1},
    [NGLI_FORMAT_R8G8B8A8_SINT]       = {4, 1 + 1 + 1 + 1},
    [NGLI_FORMAT_R8G8B8A8_SRGB]       = {4, 1 + 1 + 1 + 1},
    [NGLI_FORMAT_B8G8R8A8_UNORM]      = {4, 1 + 1 + 1 + 1},
    [NGLI_FORMAT_B8G8R8A8_SNORM]      = {4, 1 + 1 + 1 + 1},
    [NGLI_FORMAT_B8G8R8A8_UINT]       = {4, 1 + 1 + 1 + 1},
    [NGLI_FORMAT_B8G8R8A8_SINT]       = {4, 1 + 1 + 1 + 1},
    [NGLI_FORMAT_R16_UNORM]           = {1, 2},
    [NGLI_FORMAT_R16_SNORM]           = {1, 2},
    [NGLI_FORMAT_R16_UINT]            = {1, 2},
    [NGLI_FORMAT_R16_SINT]            = {1, 2},
    [NGLI_FORMAT_R16_SFLOAT]          = {1, 2},
    [NGLI_FORMAT_R16G16_UNORM]        = {2, 2 + 2},
    [NGLI_FORMAT_R16G16_SNORM]        = {2, 2 + 2},
    [NGLI_FORMAT_R16G16_UINT]         = {2, 2 + 2},
    [NGLI_FORMAT_R16G16_SINT]         = {2, 2 + 2},
    [NGLI_FORMAT_R16G16_SFLOAT]       = {2, 2 + 2},
    [NGLI_FORMAT_R16G16B16_UNORM]     = {3, 2 + 2 + 2},
    [NGLI_FORMAT_R16G16B16_SNORM]     = {3, 2 + 2 + 2},
    [NGLI_FORMAT_R16G16B16_UINT]      = {3, 2 + 2 + 2},
    [NGLI_FORMAT_R16G16B16_SINT]      = {3, 2 + 2 + 2},
    [NGLI_FORMAT_R16G16B16_SFLOAT]    = {3, 2 + 2 + 2},
    [NGLI_FORMAT_R16G16B16A16_UNORM]  = {4, 2 + 2 + 2 + 2},
    [NGLI_FORMAT_R16G16B16A16_SNORM]  = {4, 2 + 2 + 2 + 2},
    [NGLI_FORMAT_R16G16B16A16_UINT]   = {4, 2 + 2 + 2 + 2},
    [NGLI_FORMAT_R16G16B16A16_SINT]   = {4, 2 + 2 + 2 + 2},
    [NGLI_FORMAT_R16G16B16A16_SFLOAT] = {4, 2 + 2 + 2 + 2},
    [NGLI_FORMAT_R32_UINT]            = {1, 4},
    [NGLI_FORMAT_R32_SINT]            = {1, 4},
    [NGLI_FORMAT_R64_SINT]            = {1, 8},
    [NGLI_FORMAT_R32_SFLOAT]          = {1, 4},
    [NGLI_FORMAT_R32G32_UINT]         = {2, 4 + 4},
    [NGLI_FORMAT_R32G32_SINT]         = {2, 4 + 4},
    [NGLI_FORMAT_R32G32_SFLOAT]       = {2, 4 + 4},
    [NGLI_FORMAT_R32G32B32_UINT]      = {3, 4 + 4 + 4},
    [NGLI_FORMAT_R32G32B32_SINT]      = {3, 4 + 4 + 4},
    [NGLI_FORMAT_R32G32B32_SFLOAT]    = {3, 4 + 4 + 4},
    [NGLI_FORMAT_R32G32B32A32_UINT]   = {4, 4 + 4 + 4 + 4},
    [NGLI_FORMAT_R32G32B32A32_SINT]   = {4, 4 + 4 + 4 + 4},
    [NGLI_FORMAT_R32G32B32A32_SFLOAT] = {4, 4 + 4 + 4 + 4},
    [NGLI_FORMAT_D16_UNORM]           = {1, 2},
    [NGLI_FORMAT_X8_D24_UNORM_PACK32] = {2, 3 + 1},
    [NGLI_FORMAT_D32_SFLOAT]          = {1, 4},
    [NGLI_FORMAT_D24_UNORM_S8_UINT]   = {2, 3 + 1},
    [NGLI_FORMAT_D32_SFLOAT_S8_UINT]  = {3, 4 + 1 + 3},
    [NGLI_FORMAT_S8_UINT]             = {1, 1},
};

int ngli_format_get_bytes_per_pixel(int format)
{
    return format_comp_sizes[format].size;
}

int ngli_format_get_nb_comp(int format)
{
    return format_comp_sizes[format].nb_comp;
}

static const struct entry {
    const char *glsl_format;
    GLint format;
    GLint internal_format;
    GLenum type;
} format_map[] = {
    [NGLI_FORMAT_UNDEFINED]            = {NULL,           0,                  0,                     0},
    [NGLI_FORMAT_R8_UNORM]             = {"r8",           GL_RED,             GL_R8,                 GL_UNSIGNED_BYTE},
    [NGLI_FORMAT_R8_SNORM]             = {"r8_snorm",     GL_RED,             GL_R8_SNORM,           GL_BYTE},
    [NGLI_FORMAT_R8_UINT]              = {"r8_ui",        GL_RED_INTEGER,     GL_R8UI,               GL_UNSIGNED_BYTE},
    [NGLI_FORMAT_R8_SINT]              = {"r8_i",         GL_RED_INTEGER,     GL_R8I,                GL_BYTE},
    [NGLI_FORMAT_R8G8_UNORM]           = {"rg8",          GL_RG,              GL_RG8,                GL_UNSIGNED_BYTE},
    [NGLI_FORMAT_R8G8_SNORM]           = {"rg8_snorm",    GL_RG,              GL_RG8_SNORM,          GL_BYTE},
    [NGLI_FORMAT_R8G8_UINT]            = {"rg8_ui",       GL_RG_INTEGER,      GL_RG8UI,              GL_UNSIGNED_BYTE},
    [NGLI_FORMAT_R8G8_SINT]            = {"rg8_i",        GL_RG_INTEGER,      GL_RG8I,               GL_BYTE},
    [NGLI_FORMAT_R8G8B8_UNORM]         = {NULL,           GL_RGB,             GL_RGB8,               GL_UNSIGNED_BYTE},
    [NGLI_FORMAT_R8G8B8_SNORM]         = {NULL,           GL_RGB,             GL_RGB8_SNORM,         GL_BYTE},
    [NGLI_FORMAT_R8G8B8_UINT]          = {NULL,           GL_RGB_INTEGER,     GL_RGB8UI,             GL_UNSIGNED_BYTE},
    [NGLI_FORMAT_R8G8B8_SINT]          = {NULL,           GL_RGB_INTEGER,     GL_RGB8I,              GL_BYTE},
    [NGLI_FORMAT_R8G8B8_SRGB]          = {NULL,           GL_RGB,             GL_SRGB8,              GL_UNSIGNED_BYTE},
    [NGLI_FORMAT_R8G8B8A8_UNORM]       = {"rgba8",        GL_RGBA,            GL_RGBA8,              GL_UNSIGNED_BYTE},
    [NGLI_FORMAT_R8G8B8A8_SNORM]       = {"rgba8_snorm",  GL_RGBA,            GL_RGBA8_SNORM,        GL_BYTE},
    [NGLI_FORMAT_R8G8B8A8_UINT]        = {"rgba8ui",      GL_RGBA_INTEGER,    GL_RGBA8UI,            GL_UNSIGNED_BYTE},
    [NGLI_FORMAT_R8G8B8A8_SINT]        = {"rgba8i",       GL_RGBA_INTEGER,    GL_RGBA8I,             GL_BYTE},
    [NGLI_FORMAT_R8G8B8A8_SRGB]        = {NULL,           GL_RGBA,            GL_SRGB8_ALPHA8,       GL_UNSIGNED_BYTE},
    [NGLI_FORMAT_B8G8R8A8_UNORM]       = {"rgba8",        GL_BGRA,            GL_RGBA8,              GL_UNSIGNED_BYTE},
    [NGLI_FORMAT_B8G8R8A8_SNORM]       = {"rgba8_snorm",  GL_BGRA,            GL_RGBA8_SNORM,        GL_BYTE},
    [NGLI_FORMAT_B8G8R8A8_UINT]        = {"rgba8ui",      GL_BGRA_INTEGER,    GL_RGBA8UI,            GL_UNSIGNED_BYTE},
    [NGLI_FORMAT_B8G8R8A8_SINT]        = {"rgba8i",       GL_BGRA_INTEGER,    GL_RGBA8I,             GL_BYTE},
    [NGLI_FORMAT_R16_UNORM]            = {"r16",          GL_RED,             GL_R16,                GL_UNSIGNED_SHORT},
    [NGLI_FORMAT_R16_SNORM]            = {"r16_snorm",    GL_RED,             GL_R16_SNORM,          GL_SHORT},
    [NGLI_FORMAT_R16_UINT]             = {"r16ui",        GL_RED_INTEGER,     GL_R16UI,              GL_UNSIGNED_SHORT},
    [NGLI_FORMAT_R16_SINT]             = {"r16i",         GL_RED_INTEGER,     GL_R16I,               GL_SHORT},
    [NGLI_FORMAT_R16_SFLOAT]           = {"r16f",         GL_RED,             GL_R16F,               GL_HALF_FLOAT},
    [NGLI_FORMAT_R16G16_UNORM]         = {"rg16",         GL_RG,              GL_RG16,               GL_UNSIGNED_SHORT},
    [NGLI_FORMAT_R16G16_SNORM]         = {"rg16_snorm",   GL_RG,              GL_RG16_SNORM,         GL_SHORT},
    [NGLI_FORMAT_R16G16_UINT]          = {"rg16ui",       GL_RG_INTEGER,      GL_RG16UI,             GL_UNSIGNED_SHORT},
    [NGLI_FORMAT_R16G16_SINT]          = {"rg16i",        GL_RG_INTEGER,      GL_RG16I,              GL_SHORT},
    [NGLI_FORMAT_R16G16_SFLOAT]        = {"rg16f",        GL_RG,              GL_RG16F,              GL_HALF_FLOAT},
    [NGLI_FORMAT_R16G16B16_UNORM]      = {NULL,           GL_RGB,             GL_RGB16,              GL_UNSIGNED_SHORT},
    [NGLI_FORMAT_R16G16B16_SNORM]      = {NULL,           GL_RGB,             GL_RGB16_SNORM,        GL_SHORT},
    [NGLI_FORMAT_R16G16B16_UINT]       = {NULL,           GL_RGB_INTEGER,     GL_RGB16UI,            GL_UNSIGNED_SHORT},
    [NGLI_FORMAT_R16G16B16_SINT]       = {NULL,           GL_RGB_INTEGER,     GL_RGB16I,             GL_SHORT},
    [NGLI_FORMAT_R16G16B16_SFLOAT]     = {NULL,           GL_RGB,             GL_RGB16F,             GL_HALF_FLOAT},
    [NGLI_FORMAT_R16G16B16A16_UNORM]   = {"rgba16",       GL_RGBA,            GL_RGBA16,             GL_UNSIGNED_SHORT},
    [NGLI_FORMAT_R16G16B16A16_SNORM]   = {"rgba16_snorm", GL_RGBA,            GL_RGBA16_SNORM,       GL_SHORT},
    [NGLI_FORMAT_R16G16B16A16_UINT]    = {"rgba16ui",     GL_RGBA_INTEGER,    GL_RGBA16UI,           GL_UNSIGNED_SHORT},
    [NGLI_FORMAT_R16G16B16A16_SINT]    = {"rgba16i",      GL_RGBA_INTEGER,    GL_RGBA16I,            GL_SHORT},
    [NGLI_FORMAT_R16G16B16A16_SFLOAT]  = {"rgba16f",      GL_RGBA,            GL_RGBA16F,            GL_HALF_FLOAT},
    [NGLI_FORMAT_R32_UINT]             = {"r32ui",        GL_RED_INTEGER,     GL_R32UI,              GL_UNSIGNED_INT},
    [NGLI_FORMAT_R32_SINT]             = {"r32i",         GL_RED_INTEGER,     GL_R32I,               GL_INT},
    [NGLI_FORMAT_R32_SFLOAT]           = {"r32f",         GL_RED,             GL_R32F,               GL_FLOAT},
    [NGLI_FORMAT_R32G32_UINT]          = {"rg32ui",       GL_RG_INTEGER,      GL_RG32UI,             GL_UNSIGNED_INT},
    [NGLI_FORMAT_R32G32_SINT]          = {"rg32i",        GL_RG_INTEGER,      GL_RG32I,              GL_INT},
    [NGLI_FORMAT_R32G32_SFLOAT]        = {"rg32f",        GL_RG,              GL_RG32F,              GL_FLOAT},
    [NGLI_FORMAT_R32G32B32_UINT]       = {NULL,           GL_RGB_INTEGER,     GL_RGB32UI,            GL_UNSIGNED_INT},
    [NGLI_FORMAT_R32G32B32_SINT]       = {NULL,           GL_RGB_INTEGER,     GL_RGB32I,             GL_INT},
    [NGLI_FORMAT_R32G32B32_SFLOAT]     = {NULL,           GL_RGB,             GL_RGB32F,             GL_FLOAT},
    [NGLI_FORMAT_R32G32B32A32_UINT]    = {"rgba32ui",     GL_RGBA_INTEGER,    GL_RGBA32UI,           GL_UNSIGNED_INT},
    [NGLI_FORMAT_R32G32B32A32_SINT]    = {"rgba32i",      GL_RGBA_INTEGER,    GL_RGBA32I,            GL_INT},
    [NGLI_FORMAT_R32G32B32A32_SFLOAT]  = {"rgba32f",      GL_RGBA,            GL_RGBA32F,            GL_FLOAT},
    [NGLI_FORMAT_D16_UNORM]            = {NULL,           GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT16,  GL_UNSIGNED_SHORT},
    [NGLI_FORMAT_X8_D24_UNORM_PACK32]  = {NULL,           GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT24,  GL_UNSIGNED_INT},
    [NGLI_FORMAT_D32_SFLOAT]           = {NULL,           GL_DEPTH_COMPONENT, GL_DEPTH_COMPONENT32F, GL_FLOAT},
    [NGLI_FORMAT_D24_UNORM_S8_UINT]    = {NULL,           GL_DEPTH_STENCIL,   GL_DEPTH24_STENCIL8,   GL_UNSIGNED_INT_24_8},
    [NGLI_FORMAT_D32_SFLOAT_S8_UINT]   = {NULL,           GL_DEPTH_STENCIL,   GL_DEPTH32F_STENCIL8,  GL_FLOAT_32_UNSIGNED_INT_24_8_REV},
    [NGLI_FORMAT_S8_UINT]              = {NULL,           GL_STENCIL_INDEX,   GL_STENCIL_INDEX8,     GL_UNSIGNED_BYTE},
};

const char *ngli_format_get_glsl_format(int format)
{
    return format_map[format].glsl_format;
}

static int get_gl_format_type(struct glcontext *gl, int data_format,
                              GLint *formatp, GLint *internal_formatp, GLenum *typep)
{
    ngli_assert(data_format >= 0 && data_format < NGLI_ARRAY_NB(format_map));
    const struct entry *entry = &format_map[data_format];

    ngli_assert(data_format == NGLI_FORMAT_UNDEFINED ||
               (entry->format && entry->internal_format && entry->type));

    if (formatp)
        *formatp = entry->format;
    if (internal_formatp)
        *internal_formatp = entry->internal_format;
    if (typep)
        *typep = entry->type;

    return 0;
}

int ngli_format_get_gl_texture_format(struct glcontext *gl, int data_format,
                                      GLint *formatp, GLint *internal_formatp, GLenum *typep)
{
    GLint format;
    GLint internal_format;
    GLenum type;

    int ret = get_gl_format_type(gl, data_format, &format, &internal_format, &type);
    if (ret < 0)
        return ret;

    if (gl->backend == NGL_BACKEND_OPENGLES && gl->version < 300) {
        if (format == GL_RED)
            format = GL_LUMINANCE;
        else if (format == GL_RG)
            format = GL_LUMINANCE_ALPHA;
        internal_format = format == GL_BGRA ? GL_RGBA : format;
    }

    if (formatp)
        *formatp = format;
    if (internal_formatp)
        *internal_formatp = internal_format;
    if (typep)
        *typep = type;

    return 0;
}

int ngli_format_get_gl_renderbuffer_format(struct glcontext *gl, int data_format, GLint *formatp)
{
    return get_gl_format_type(gl, data_format, NULL, formatp, NULL);
}

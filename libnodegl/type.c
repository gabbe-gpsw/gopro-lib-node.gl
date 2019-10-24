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

#include "type.h"
#include "format.h"

static int gl_type_map[] = {
    [NGLI_TYPE_INT]                         = GL_INT,
    [NGLI_TYPE_IVEC2]                       = GL_INT_VEC2,
    [NGLI_TYPE_IVEC3]                       = GL_INT_VEC3,
    [NGLI_TYPE_IVEC4]                       = GL_INT_VEC4,
    [NGLI_TYPE_UINT]                        = GL_UNSIGNED_INT,
    [NGLI_TYPE_UIVEC2]                      = GL_UNSIGNED_INT_VEC2,
    [NGLI_TYPE_UIVEC3]                      = GL_UNSIGNED_INT_VEC3,
    [NGLI_TYPE_UIVEC4]                      = GL_UNSIGNED_INT_VEC4,
    [NGLI_TYPE_FLOAT]                       = GL_FLOAT,
    [NGLI_TYPE_VEC2]                        = GL_FLOAT_VEC2,
    [NGLI_TYPE_VEC3]                        = GL_FLOAT_VEC3,
    [NGLI_TYPE_VEC4]                        = GL_FLOAT_VEC4,
    [NGLI_TYPE_MAT3]                        = GL_FLOAT_MAT3,
    [NGLI_TYPE_MAT4]                        = GL_FLOAT_MAT4,
    [NGLI_TYPE_BOOL]                        = GL_BOOL,
    [NGLI_TYPE_SAMPLER_2D]                  = GL_SAMPLER_2D,
    [NGLI_TYPE_SAMPLER_2D_RECT]             = GL_SAMPLER_2D_RECT,
    [NGLI_TYPE_SAMPLER_3D]                  = GL_SAMPLER_3D,
    [NGLI_TYPE_SAMPLER_CUBE]                = GL_SAMPLER_CUBE,
    [NGLI_TYPE_SAMPLER_EXTERNAL_OES]        = GL_SAMPLER_EXTERNAL_OES,
    [NGLI_TYPE_SAMPLER_EXTERNAL_2D_Y2Y_EXT] = GL_SAMPLER_EXTERNAL_2D_Y2Y_EXT,
    [NGLI_TYPE_IMAGE_2D]                    = GL_IMAGE_2D,
    [NGLI_TYPE_UNIFORM_BUFFER]              = GL_UNIFORM_BUFFER,
    [NGLI_TYPE_STORAGE_BUFFER]              = GL_SHADER_STORAGE_BUFFER,
};

GLenum ngli_type_get_gl_type(int type)
{
    return gl_type_map[type];
}

static const struct {
    int is_sampler_or_image;
    const char *glsl_type;
} type_info_map[] = {
    [NGLI_TYPE_INT]                         = {0, "int"},
    [NGLI_TYPE_IVEC2]                       = {0, "ivec2"},
    [NGLI_TYPE_IVEC3]                       = {0, "ivec3"},
    [NGLI_TYPE_IVEC4]                       = {0, "ivec4"},
    [NGLI_TYPE_UINT]                        = {0, "uint"},
    [NGLI_TYPE_UIVEC2]                      = {0, "uvec2"},
    [NGLI_TYPE_UIVEC3]                      = {0, "uvec3"},
    [NGLI_TYPE_UIVEC4]                      = {0, "uvec4"},
    [NGLI_TYPE_FLOAT]                       = {0, "float"},
    [NGLI_TYPE_VEC2]                        = {0, "vec2"},
    [NGLI_TYPE_VEC3]                        = {0, "vec3"},
    [NGLI_TYPE_VEC4]                        = {0, "vec4"},
    [NGLI_TYPE_MAT3]                        = {0, "mat3"},
    [NGLI_TYPE_MAT4]                        = {0, "mat4"},
    [NGLI_TYPE_BOOL]                        = {0, "bool"},
    [NGLI_TYPE_SAMPLER_2D]                  = {1, "sampler2D"},
    [NGLI_TYPE_SAMPLER_2D_RECT]             = {1, "sampler2DRect"},
    [NGLI_TYPE_SAMPLER_3D]                  = {1, "sampler3D"},
    [NGLI_TYPE_SAMPLER_CUBE]                = {1, "samplerCube"},
    [NGLI_TYPE_SAMPLER_EXTERNAL_OES]        = {1, "samplerExternalOES"},
    [NGLI_TYPE_SAMPLER_EXTERNAL_2D_Y2Y_EXT] = {1, "__samplerExternal2DY2YEXT"},
    [NGLI_TYPE_IMAGE_2D]                    = {1, "image2D"},
    [NGLI_TYPE_UNIFORM_BUFFER]              = {0, "uniform"},
    [NGLI_TYPE_STORAGE_BUFFER]              = {0, "buffer"},
};

int ngli_type_is_sampler_or_image(int type)
{
    return type_info_map[type].is_sampler_or_image;
}

const char *ngli_type_get_glsl_type(int type)
{
    return type_info_map[type].glsl_type;
}

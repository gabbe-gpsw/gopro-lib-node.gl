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

#include "nodegl.h"
#include "nodes.h"
#include "type.h"

static const struct param_choices type_choices = {
    .name = "type",
    .consts = {
        {"none",   NGLI_TYPE_NONE,  .desc=NGLI_DOCSTRING("none")},
        {"int",    NGLI_TYPE_INT,   .desc=NGLI_DOCSTRING("integer")},
        {"ivec2",  NGLI_TYPE_IVEC2, .desc=NGLI_DOCSTRING("2 integers")},
        {"ivec3",  NGLI_TYPE_IVEC3, .desc=NGLI_DOCSTRING("3 integers")},
        {"ivec4",  NGLI_TYPE_IVEC4, .desc=NGLI_DOCSTRING("4 integers")},
        {"uint",   NGLI_TYPE_UINT,  .desc=NGLI_DOCSTRING("unsigned integer")},
        {"uivec2", NGLI_TYPE_UIVEC2,.desc=NGLI_DOCSTRING("2 unsigned integers")},
        {"uivec3", NGLI_TYPE_UIVEC3,.desc=NGLI_DOCSTRING("3 unsigned integers")},
        {"uivec4", NGLI_TYPE_UIVEC4,.desc=NGLI_DOCSTRING("4 unsigned integers")},
        {"float",  NGLI_TYPE_FLOAT, .desc=NGLI_DOCSTRING("float")},
        {"vec2",   NGLI_TYPE_VEC2,  .desc=NGLI_DOCSTRING("2 floats")},
        {"vec3",   NGLI_TYPE_VEC3,  .desc=NGLI_DOCSTRING("3 floats")},
        {"vec4",   NGLI_TYPE_VEC4,  .desc=NGLI_DOCSTRING("4 floats")},
        {"mat3",   NGLI_TYPE_MAT3,  .desc=NGLI_DOCSTRING("3x3 floats")},
        {"mat4",   NGLI_TYPE_MAT4,  .desc=NGLI_DOCSTRING("4x4 floats")},
        {"bool",   NGLI_TYPE_BOOL,  .desc=NGLI_DOCSTRING("boolean")},
        {NULL}
    }
};

#define OFFSET(x) offsetof(struct iovariable_priv, x)
static const struct node_param iovariable_params[] = {
    {"type", PARAM_TYPE_SELECT, OFFSET(type), {.i64=NGLI_TYPE_NONE},
             .flags=PARAM_FLAG_CONSTRUCTOR,
             .choices=&type_choices,
             .desc=NGLI_DOCSTRING("type qualifier for the shader")},
    {NULL}
};

const struct node_class ngli_iovariable_class = {
    .id        = NGL_NODE_IOVARIABLE,
    .name      = "IOVariable",
    .priv_size = sizeof(struct iovariable_priv),
    .params    = iovariable_params,
    .file      = __FILE__,
};

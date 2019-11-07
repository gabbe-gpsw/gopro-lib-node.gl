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

#ifndef PROGRAM_REFLECTION_H
#define PROGRAM_REFLECTION_H

#ifdef __cplusplus
extern "C" {
#endif

struct program_reflection {
    struct ngl_ctx *ctx;
    struct hmap *uniforms;
    struct hmap *blocks;
    struct hmap *inputs;
    struct hmap *outputs;
};

int ngli_program_reflection_init(struct program_reflection *s,
                                 const char *vertex,
                                 const char *fragment,
                                 const char *compute);
void ngli_program_reflection_dump(struct program_reflection *s);
void ngli_program_reflection_reset(struct program_reflection *s);

#ifdef __cplusplus
}
#endif

#endif

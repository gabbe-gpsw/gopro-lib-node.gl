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

#include <GL/glcorearb.h>
#include <glslang/Include/ResourceLimits.h>
#include <glslang/Include/Types.h>
#include <glslang/Public/ShaderLang.h>

extern "C" {
#include "hmap.h"
#include "memory.h"
#include "log.h"
#include "program_reflection.h"
#include "utils.h"
#include "texture.h"
#include "program.h"
#include "type.h"
#include "time.h"
#include "glincludes.h"
}

class GLslangInitializer {
    public:
    GLslangInitializer()
    {
        glslang::InitializeProcess();
    }

    ~GLslangInitializer()
    {
        glslang::FinalizeProcess();
    }
};

static GLslangInitializer initializer;

static const TBuiltInResource default_ressources = {
    /* .MaxLights = */ 32,
    /* .MaxClipPlanes = */ 6,
    /* .MaxTextureUnits = */ 32,
    /* .MaxTextureCoords = */ 32,
    /* .MaxVertexAttribs = */ 64,
    /* .MaxVertexUniformComponents = */ 4096,
    /* .MaxVaryingFloats = */ 64,
    /* .MaxVertexTextureImageUnits = */ 32,
    /* .MaxCombinedTextureImageUnits = */ 80,
    /* .MaxTextureImageUnits = */ 32,
    /* .MaxFragmentUniformComponents = */ 4096,
    /* .MaxDrawBuffers = */ 32,
    /* .MaxVertexUniformVectors = */ 128,
    /* .MaxVaryingVectors = */ 8,
    /* .MaxFragmentUniformVectors = */ 16,
    /* .MaxVertexOutputVectors = */ 16,
    /* .MaxFragmentInputVectors = */ 15,
    /* .MinProgramTexelOffset = */ -8,
    /* .MaxProgramTexelOffset = */ 7,
    /* .MaxClipDistances = */ 8,
    /* .MaxComputeWorkGroupCountX = */ 65535,
    /* .MaxComputeWorkGroupCountY = */ 65535,
    /* .MaxComputeWorkGroupCountZ = */ 65535,
    /* .MaxComputeWorkGroupSizeX = */ 1024,
    /* .MaxComputeWorkGroupSizeY = */ 1024,
    /* .MaxComputeWorkGroupSizeZ = */ 64,
    /* .MaxComputeUniformComponents = */ 1024,
    /* .MaxComputeTextureImageUnits = */ 16,
    /* .MaxComputeImageUniforms = */ 8,
    /* .MaxComputeAtomicCounters = */ 8,
    /* .MaxComputeAtomicCounterBuffers = */ 1,
    /* .MaxVaryingComponents = */ 60,
    /* .MaxVertexOutputComponents = */ 64,
    /* .MaxGeometryInputComponents = */ 64,
    /* .MaxGeometryOutputComponents = */ 128,
    /* .MaxFragmentInputComponents = */ 128,
    /* .MaxImageUnits = */ 8,
    /* .MaxCombinedImageUnitsAndFragmentOutputs = */ 8,
    /* .MaxCombinedShaderOutputResources = */ 8,
    /* .MaxImageSamples = */ 0,
    /* .MaxVertexImageUniforms = */ 0,
    /* .MaxTessControlImageUniforms = */ 0,
    /* .MaxTessEvaluationImageUniforms = */ 0,
    /* .MaxGeometryImageUniforms = */ 0,
    /* .MaxFragmentImageUniforms = */ 8,
    /* .MaxCombinedImageUniforms = */ 8,
    /* .MaxGeometryTextureImageUnits = */ 16,
    /* .MaxGeometryOutputVertices = */ 256,
    /* .MaxGeometryTotalOutputComponents = */ 1024,
    /* .MaxGeometryUniformComponents = */ 1024,
    /* .MaxGeometryVaryingComponents = */ 64,
    /* .MaxTessControlInputComponents = */ 128,
    /* .MaxTessControlOutputComponents = */ 128,
    /* .MaxTessControlTextureImageUnits = */ 16,
    /* .MaxTessControlUniformComponents = */ 1024,
    /* .MaxTessControlTotalOutputComponents = */ 4096,
    /* .MaxTessEvaluationInputComponents = */ 128,
    /* .MaxTessEvaluationOutputComponents = */ 128,
    /* .MaxTessEvaluationTextureImageUnits = */ 16,
    /* .MaxTessEvaluationUniformComponents = */ 1024,
    /* .MaxTessPatchComponents = */ 120,
    /* .MaxPatchVertices = */ 32,
    /* .MaxTessGenLevel = */ 64,
    /* .MaxViewports = */ 16,
    /* .MaxVertexAtomicCounters = */ 0,
    /* .MaxTessControlAtomicCounters = */ 0,
    /* .MaxTessEvaluationAtomicCounters = */ 0,
    /* .MaxGeometryAtomicCounters = */ 0,
    /* .MaxFragmentAtomicCounters = */ 8,
    /* .MaxCombinedAtomicCounters = */ 8,
    /* .MaxAtomicCounterBindings = */ 1,
    /* .MaxVertexAtomicCounterBuffers = */ 0,
    /* .MaxTessControlAtomicCounterBuffers = */ 0,
    /* .MaxTessEvaluationAtomicCounterBuffers = */ 0,
    /* .MaxGeometryAtomicCounterBuffers = */ 0,
    /* .MaxFragmentAtomicCounterBuffers = */ 1,
    /* .MaxCombinedAtomicCounterBuffers = */ 1,
    /* .MaxAtomicCounterBufferSize = */ 16384,
    /* .MaxTransformFeedbackBuffers = */ 4,
    /* .MaxTransformFeedbackInterleavedComponents = */ 64,
    /* .MaxCullDistances = */ 8,
    /* .MaxCombinedClipAndCullDistances = */ 8,
    /* .MaxSamples = */ 4,
    /* .maxMeshOutputVerticesNV = */ 256,
    /* .maxMeshOutputPrimitivesNV = */ 512,
    /* .maxMeshWorkGroupSizeX_NV = */ 32,
    /* .maxMeshWorkGroupSizeY_NV = */ 1,
    /* .maxMeshWorkGroupSizeZ_NV = */ 1,
    /* .maxTaskWorkGroupSizeX_NV = */ 32,
    /* .maxTaskWorkGroupSizeY_NV = */ 1,
    /* .maxTaskWorkGroupSizeZ_NV = */ 1,
    /* .maxMeshViewCountNV = */ 4,

    /* .limits = */ {
        /* .nonInductiveForLoops = */ 1,
        /* .whileLoops = */ 1,
        /* .doWhileLoops = */ 1,
        /* .generalUniformIndexing = */ 1,
        /* .generalAttributeMatrixVectorIndexing = */ 1,
        /* .generalVaryingIndexing = */ 1,
        /* .generalSamplerIndexing = */ 1,
        /* .generalVariableIndexing = */ 1,
        /* .generalConstantMatrixVectorIndexing = */ 1,
}};

static int add_shader(glslang::TProgram & program, EShLanguage stage, const char *text)
{
    glslang::TShader *shader = new glslang::TShader(stage);
    shader->setStrings(&text, 1);
    shader->setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);

    EShMessages messages = EShMsgDefault;
    if (!shader->parse(&default_ressources, 100, false, messages)) {
        LOG(ERROR, "failed to parse shader: %s", text);
        return NGL_ERROR_EXTERNAL;
    }
    program.addShader(shader);
    return 0;
}

static const struct {
    GLenum gl_type;
    int type;
} types_map[] = {
    {GL_INT,                         NGLI_TYPE_INT},
    {GL_INT_VEC2,                    NGLI_TYPE_IVEC2},
    {GL_INT_VEC3,                    NGLI_TYPE_IVEC3},
    {GL_INT_VEC4,                    NGLI_TYPE_IVEC4},
    {GL_UNSIGNED_INT,                NGLI_TYPE_UINT},
    {GL_UNSIGNED_INT_VEC2,           NGLI_TYPE_UIVEC2},
    {GL_UNSIGNED_INT_VEC3,           NGLI_TYPE_UIVEC3},
    {GL_UNSIGNED_INT_VEC4,           NGLI_TYPE_UIVEC4},
    {GL_FLOAT,                       NGLI_TYPE_FLOAT},
    {GL_FLOAT_VEC2,                  NGLI_TYPE_VEC2},
    {GL_FLOAT_VEC3,                  NGLI_TYPE_VEC3},
    {GL_FLOAT_VEC4,                  NGLI_TYPE_VEC4},
    {GL_FLOAT_MAT3,                  NGLI_TYPE_MAT3},
    {GL_FLOAT_MAT4,                  NGLI_TYPE_MAT4},
    {GL_BOOL,                        NGLI_TYPE_BOOL},
    {GL_SAMPLER_2D,                  NGLI_TYPE_SAMPLER_2D},
    {GL_SAMPLER_2D_RECT,             NGLI_TYPE_SAMPLER_2D_RECT},
    {GL_SAMPLER_3D,                  NGLI_TYPE_SAMPLER_3D},
    {GL_SAMPLER_CUBE,                NGLI_TYPE_SAMPLER_CUBE},
    {GL_SAMPLER_EXTERNAL_OES,        NGLI_TYPE_SAMPLER_EXTERNAL_OES},
    {GL_SAMPLER_EXTERNAL_2D_Y2Y_EXT, NGLI_TYPE_SAMPLER_EXTERNAL_2D_Y2Y_EXT},
    {GL_IMAGE_2D,                    NGLI_TYPE_IMAGE_2D},
    {GL_UNIFORM_BUFFER,              NGLI_TYPE_UNIFORM_BUFFER},
    {GL_SHADER_STORAGE_BUFFER,       NGLI_TYPE_STORAGE_BUFFER},
};

static int get_type(GLenum gl_type)
{
    for (int i = 0; i < NGLI_ARRAY_NB(types_map); i++)
        if (types_map[i].gl_type == gl_type)
            return types_map[i].type;
    return NGLI_TYPE_NONE;
}

static void copy_reflection_object(struct program_variable_info *dst, const glslang::TObjectReflection *src)
{
    dst->type     = get_type(src->glDefineType);
    dst->size     = src->size;
    dst->location = -1;
    dst->binding  = src->getBinding();
    dst->offset   = src->offset;
    dst->index    = src->index;

    /* stages */
    dst->stages = 0;
    if (src->stages & EShLangVertexMask)
        dst->stages |= (1 << NGLI_PROGRAM_SHADER_VERT);
    if (src->stages & EShLangFragmentMask)
        dst->stages |= (1 << NGLI_PROGRAM_SHADER_FRAG);
    if (src->stages & EShLangComputeMask)
        dst->stages |= (1 << NGLI_PROGRAM_SHADER_COMP);

    /* access */
    const glslang::TType *type = src->getType();
    if (type) {
        const glslang::TQualifier & qualifier = type->getQualifier();
        if (qualifier.hasLayout()) {
            if (qualifier.hasAnyLocation()) {
                dst->location = qualifier.layoutLocation;
            }
        }
        if (qualifier.readonly)
            dst->access = NGLI_ACCESS_READ_BIT;
        else if (qualifier.writeonly)
            dst->access = NGLI_ACCESS_WRITE_BIT;
        else
            dst->access = NGLI_ACCESS_READ_WRITE;
    }
}

int ngli_program_reflection_init(struct program_reflection *s,
                                 const char *vertex,
                                 const char *fragment,
                                 const char *compute)
{
    int ret = 0;
    int count = 0;
    const int reflection_options = EShReflectionDefault | EShReflectionSeparateBuffers;

    EShMessages messages = EShMsgDefault;
    glslang::TProgram program;

    if (vertex) {
        ret = add_shader(program, EShLangVertex, vertex);
        if (ret < 0)
            goto done;
    }

    if (fragment) {
        ret = add_shader(program, EShLangFragment, fragment);
        if (ret < 0)
            goto done;
    }

    if (compute) {
        ret = add_shader(program, EShLangCompute, compute);
        if (ret < 0)
            goto done;
    }


    if (!program.link(messages)) {
        LOG(ERROR, "could not link program");
        ret = NGL_ERROR_EXTERNAL;
        goto done;
    }

    if (!program.buildReflection(reflection_options)) {
        LOG(ERROR, "could not build program reflection");
        ret = NGL_ERROR_EXTERNAL;
        goto done;
    }

    s->uniforms = ngli_hmap_create();
    s->blocks = ngli_hmap_create();
    s->inputs = ngli_hmap_create();
    s->outputs = ngli_hmap_create();
    if (!s->uniforms || !s->blocks || !s->inputs || !s->outputs) {
        ret = NGL_ERROR_MEMORY;
        goto done;
    }

    count = program.getNumLiveUniformVariables();
    for (int i = 0; i < count; i++) {
        const glslang::TObjectReflection &src = program.getUniform(i);
        struct program_variable_info *dst = (struct program_variable_info *)ngli_malloc(sizeof(*dst));
        if (!dst) {
            goto done;
        }
        copy_reflection_object(dst, &src);
        ret = ngli_hmap_set(s->uniforms, src.name.c_str(), dst);
        if (ret < 0)
            goto done;
    }

    count = program.getNumUniformBlocks();
    for (int i = 0; i < count; i++) {
        const glslang::TObjectReflection &src = program.getUniformBlock(i);
        struct program_variable_info *dst = (struct program_variable_info *)ngli_malloc(sizeof(*dst));
        if (!dst) {
            goto done;
        }
        copy_reflection_object(dst, &src);
        ret = ngli_hmap_set(s->blocks, src.name.c_str(), dst);
        if (ret < 0)
            goto done;
        dst->type = NGLI_TYPE_UNIFORM_BUFFER;
    }

    count = program.getNumBufferBlocks();
    for (int i = 0; i < count; i++) {
        const glslang::TObjectReflection &src = program.getBufferBlock(i);
        struct program_variable_info *dst = (struct program_variable_info *)ngli_malloc(sizeof(*dst));
        if (!dst) {
            goto done;
        }
        copy_reflection_object(dst, &src);
        ret = ngli_hmap_set(s->blocks, src.name.c_str(), dst);
        if (ret < 0)
            goto done;
        dst->type = NGLI_TYPE_STORAGE_BUFFER;
    }

    count = program.getNumBufferVariables();
    for (int i = 0; i < count; i++) {
        const glslang::TObjectReflection &src = program.getBufferBlock(i);
        struct program_variable_info *dst = (struct program_variable_info *)ngli_malloc(sizeof(*dst));
        if (!dst) {
            goto done;
        }
        copy_reflection_object(dst, &src);
        ret = ngli_hmap_set(s->blocks, src.name.c_str(), dst);
        if (ret < 0)
            goto done;
        dst->type = NGLI_TYPE_STORAGE_BUFFER;
    }

    count = program.getNumPipeInputs();
    for (int i = 0; i < count; i++) {
        const glslang::TObjectReflection &src = program.getPipeInput(i);
        struct program_variable_info *dst = (struct program_variable_info *)ngli_malloc(sizeof(*dst));
        if (!dst) {
            goto done;
        }
        copy_reflection_object(dst, &src);
        ret = ngli_hmap_set(s->inputs, src.name.c_str(), dst);
        if (ret < 0)
            goto done;
    }

    count = program.getNumPipeOutputs();
    for (int i = 0; i < count; i++) {
        const glslang::TObjectReflection &src = program.getPipeOutput(i);
        struct program_variable_info *dst = (struct program_variable_info *)ngli_malloc(sizeof(*dst));
        if (!dst) {
            goto done;
        }
        copy_reflection_object(dst, &src);
        ret = ngli_hmap_set(s->outputs, src.name.c_str(), dst);
        if (ret < 0) 
            goto done;
    }

done:
    return ret;
}

void ngli_program_reflection_dump(struct program_reflection *s)
{
    const struct {
        const char *name;
        struct hmap *map;
    } types[4] = {
        {"Pipeline inputs",  s->inputs},
        {"Pipeline outputs", s->outputs},
        {"Uniforms",         s->uniforms},
        {"Blocks",           s->blocks},
    };

    for (int i = 0; i < NGLI_ARRAY_NB(types); i++) {
        struct hmap *map = types[i].map;
        if (!map)
            continue;
        LOG(INFO, "%s:", types[i].name);
        const struct hmap_entry *entry = NULL;
        while ((entry = ngli_hmap_next(map, entry)) != NULL) {
          struct program_variable_info *info = (struct program_variable_info *)entry->data;
          LOG(INFO, "\tname=%s size=%d offset=%d index=%d type=0x%x location=%d binding=%d stages=0x%x access=0x%x",
              entry->key, info->size, info->offset, info->index, info->type, info->location, info->binding, info->stages, info->access);
        }
    }
}

void ngli_program_reflection_reset(struct program_reflection *s)
{
    ngli_hmap_freep(&s->uniforms);
    ngli_hmap_freep(&s->blocks);
    ngli_hmap_freep(&s->inputs);
    ngli_hmap_freep(&s->outputs);
}

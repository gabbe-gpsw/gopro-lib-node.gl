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

#include <string.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "buffer.h"
#include "nodes.h"

#define USAGE (VK_BUFFER_USAGE_VERTEX_BUFFER_BIT  | \
               VK_BUFFER_USAGE_INDEX_BUFFER_BIT   | \
               VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | \
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT)  \

int ngli_buffer_init(struct buffer *s, struct ngl_ctx *ctx, int size, int usage)
{
    struct vkcontext *vk = ctx->vkcontext;

    s->ctx = ctx;
    s->size = size;
    s->usage = usage;

    VkBufferCreateInfo buffer_create_info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = USAGE,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    VkResult vkret = vkCreateBuffer(vk->device, &buffer_create_info, NULL, &s->vkbuf);
    if (vkret != VK_SUCCESS)
        return -1;

    VkMemoryRequirements mem_req;
    vkGetBufferMemoryRequirements(vk->device, s->vkbuf, &mem_req);
    VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    uint32_t memory_type_index = ngli_vk_find_memory_type(vk, mem_req.memoryTypeBits, props);

    VkMemoryAllocateInfo alloc_info = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = mem_req.size,
        .memoryTypeIndex = memory_type_index,
    };
    vkret = vkAllocateMemory(vk->device, &alloc_info, NULL, &s->vkmem);
    if (vkret != VK_SUCCESS)
        return -1;

    vkret = vkBindBufferMemory(vk->device, s->vkbuf, s->vkmem, 0);
    if (vkret != VK_SUCCESS)
        return -1;

    return 0;
}

int ngli_buffer_upload(struct buffer *s, const void *data, int size)
{
    struct ngl_ctx *ctx = s->ctx;
    struct vkcontext *vk = ctx->vkcontext;
    void *mapped_mem;
    VkResult ret = vkMapMemory(vk->device, s->vkmem, 0, size, 0, &mapped_mem);
    if (ret != VK_SUCCESS)
        return -1;
    memcpy(mapped_mem, data, size);
    vkUnmapMemory(vk->device, s->vkmem);
    return 0;
}

void ngli_buffer_reset(struct buffer *s)
{
    struct ngl_ctx *ctx = s->ctx;
    if (!ctx)
        return;
    struct vkcontext *vk = ctx->vkcontext;
    vkDestroyBuffer(vk->device, s->vkbuf, NULL);
    vkFreeMemory(vk->device, s->vkmem, NULL);
    memset(s, 0, sizeof(*s));
}

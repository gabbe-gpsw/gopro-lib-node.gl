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

#include <string.h>
#include <vulkan/vulkan.h>

#include "gtimer.h"
#include "memory.h"
#include "nodes.h"
#include "gctx_vk.h"

int ngli_gtimer_init(struct gtimer *s, struct ngl_ctx *ctx)
{
    s->ctx = ctx;

    struct vkcontext *vk = ctx->vkcontext;

    const VkQueryPoolCreateInfo query_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = 2,
    };
    VkResult vkret = vkCreateQueryPool(vk->device, &query_pool_create_info, NULL, &s->query_pool);
    if (vkret != VK_SUCCESS)
        return -1;

    return 0;
}

static int write_ts(struct gtimer *s, int query)
{
    struct vkcontext *vk = s->ctx->vkcontext;

    // XXX: we are always comparing from the top of the pipe; is this correct?
    VkCommandBuffer command_buffer = vk->cur_command_buffer;
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, s->query_pool, query);

    return 0;
}

int ngli_gtimer_start(struct gtimer *s)
{
    struct vkcontext *vk = s->ctx->vkcontext;

    ngli_gctx_vk_end_render_pass(s->ctx);

    VkCommandBuffer command_buffer = vk->cur_command_buffer;
    vkCmdResetQueryPool(command_buffer, s->query_pool, 0, 2);

    return write_ts(s, 0);
}

int ngli_gtimer_stop(struct gtimer *s)
{
    //struct vkcontext *vk = s->ctx->vkcontext;
    ngli_gctx_vk_end_render_pass(s->ctx);

    int ret = write_ts(s, 1);
    if (ret < 0)
        return ret;

    // XXX: extra security, could be dropped
    memset(s->results, 0, sizeof(s->results));

    // XXX: we're probably pulling previous results here
    // TODO: check code return
    // XXX: the parameters are somehow redundants, why?
    /*
    vkGetQueryPoolResults(vk->device, s->query_pool, 0, 2, sizeof(s->results),
                          s->results, sizeof(s->results[0]), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
                          */

    return 0;
}

int64_t ngli_gtimer_read(struct gtimer *s)
{
    //XXX: wrong unit?
    //LOG(ERROR, "results: %ld %ld -> %ld", s->results[0], s->results[1], s->results[1] - s->results[0]);
    return s->results[1] - s->results[0];
}

void ngli_gtimer_reset(struct gtimer *s)
{
    if (!s->ctx)
        return;

    struct vkcontext *vk = s->ctx->vkcontext;
    vkDestroyQueryPool(vk->device, s->query_pool, NULL);
    memset(s, 0, sizeof(*s));
}

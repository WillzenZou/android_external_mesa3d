/*
 * Copyright © 2020 Google, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef PANVK_VX_CMD_BUFFER_H
#define PANVK_VX_CMD_BUFFER_H

#ifndef PAN_ARCH
#error "panvk_vX_cmd_buffer.h is a per-gen header"
#endif

#include <stdarg.h>
#include <stdlib.h>

#include "panvk_cmd_buffer.h"

struct cs_builder;

enum panvk_csf_queue_id {
   PANVK_CSF_VERTEX_TILING_QUEUE = 0,
   PANVK_CSF_FRAGMENT_QUEUE,
   PANVK_CSF_COMPUTE_QUEUE,
   PANVK_CSF_QUEUE_COUNT,
};

struct panvk_cs {
   uint32_t latest_flush_id;
   struct cs_builder *builder;
};

struct panvk_csf_cmd_buffer {
   struct panvk_cmd_buffer base;
   mali_ptr tiler_desc;
   struct panvk_cs streams[PANVK_CSF_QUEUE_COUNT];
};

VK_DEFINE_HANDLE_CASTS(panvk_csf_cmd_buffer, base.vk.base, VkCommandBuffer,
                       VK_OBJECT_TYPE_COMMAND_BUFFER)

#endif /* PANVK_VX_CMD_BUFFER_H */

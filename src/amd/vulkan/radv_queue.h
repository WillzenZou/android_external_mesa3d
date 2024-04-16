/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef RADV_QUEUE_H
#define RADV_QUEUE_H

#include "vk_queue.h"

#include "radv_radeon_winsys.h"

struct radv_device;

struct radv_queue_ring_info {
   uint32_t scratch_size_per_wave;
   uint32_t scratch_waves;
   uint32_t compute_scratch_size_per_wave;
   uint32_t compute_scratch_waves;
   uint32_t esgs_ring_size;
   uint32_t gsvs_ring_size;
   uint32_t attr_ring_size;
   bool tess_rings;
   bool task_rings;
   bool mesh_scratch_ring;
   bool gds;
   bool gds_oa;
   bool sample_positions;
};

enum radv_queue_family {
   RADV_QUEUE_GENERAL,
   RADV_QUEUE_COMPUTE,
   RADV_QUEUE_TRANSFER,
   RADV_QUEUE_SPARSE,
   RADV_QUEUE_VIDEO_DEC,
   RADV_QUEUE_VIDEO_ENC,
   RADV_MAX_QUEUE_FAMILIES,
   RADV_QUEUE_FOREIGN = RADV_MAX_QUEUE_FAMILIES,
   RADV_QUEUE_IGNORED,
};

struct radv_queue_state {
   enum radv_queue_family qf;
   struct radv_queue_ring_info ring_info;

   struct radeon_winsys_bo *scratch_bo;
   struct radeon_winsys_bo *descriptor_bo;
   struct radeon_winsys_bo *compute_scratch_bo;
   struct radeon_winsys_bo *esgs_ring_bo;
   struct radeon_winsys_bo *gsvs_ring_bo;
   struct radeon_winsys_bo *tess_rings_bo;
   struct radeon_winsys_bo *task_rings_bo;
   struct radeon_winsys_bo *mesh_scratch_ring_bo;
   struct radeon_winsys_bo *attr_ring_bo;
   struct radeon_winsys_bo *gds_bo;
   struct radeon_winsys_bo *gds_oa_bo;

   struct radeon_cmdbuf *initial_preamble_cs;
   struct radeon_cmdbuf *initial_full_flush_preamble_cs;
   struct radeon_cmdbuf *continue_preamble_cs;
   struct radeon_cmdbuf *gang_wait_preamble_cs;
   struct radeon_cmdbuf *gang_wait_postamble_cs;

   /* the uses_shadow_regs here will be set only for general queue */
   bool uses_shadow_regs;
   /* register state is saved in shadowed_regs buffer */
   struct radeon_winsys_bo *shadowed_regs;
   /* shadow regs preamble ib. This will be the first preamble ib.
    * This ib has the packets to start register shadowing.
    */
   struct radeon_winsys_bo *shadow_regs_ib;
   uint32_t shadow_regs_ib_size_dw;
};

struct radv_queue {
   struct vk_queue vk;
   struct radv_device *device;
   struct radeon_winsys_ctx *hw_ctx;
   enum radeon_ctx_priority priority;
   struct radv_queue_state state;
   struct radv_queue_state *follower_state;
   struct radeon_winsys_bo *gang_sem_bo;

   uint64_t last_shader_upload_seq;
   bool sqtt_present;
};

VK_DEFINE_HANDLE_CASTS(radv_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

static inline struct radv_device *
radv_queue_device(const struct radv_queue *queue)
{
   return (struct radv_device *)queue->vk.base.device;
}

int radv_queue_init(struct radv_device *device, struct radv_queue *queue, int idx,
                    const VkDeviceQueueCreateInfo *create_info,
                    const VkDeviceQueueGlobalPriorityCreateInfoKHR *global_priority);

void radv_queue_finish(struct radv_queue *queue);

enum radeon_ctx_priority radv_get_queue_global_priority(const VkDeviceQueueGlobalPriorityCreateInfoKHR *pObj);

bool radv_queue_internal_submit(struct radv_queue *queue, struct radeon_cmdbuf *cs);

#endif /* RADV_QUEUE_H */

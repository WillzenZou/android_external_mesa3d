/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_VX_QUEUE_H
#define PANVK_VX_QUEUE_H

#ifndef PAN_ARCH
#error "panvk_vX_queue.h is a per-gen header"
#endif

#include <stdint.h>

#include "vk_queue.h"

struct panvk_queue {
   struct vk_queue vk;

   /* Number of CS queues */
   int pqueue_count;

   /* Sync timeline */
   struct {
      uint32_t handle;
      uint64_t point;
   } sync;
};

VK_DEFINE_HANDLE_CASTS(panvk_queue, vk.base, VkQueue, VK_OBJECT_TYPE_QUEUE)

#endif /* PANVK_VX_QUEUE_H */

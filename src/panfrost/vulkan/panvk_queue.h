/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_QUEUE_H
#define PANVK_QUEUE_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "vk_queue.h"

#if PAN_ARCH >= 10
#include "csf/panvk_vX_queue.h"
#else
#include "jm/panvk_vX_queue.h"
#endif

static inline struct panvk_device *
panvk_queue_get_device(const struct panvk_queue *queue)
{
   return container_of(queue->vk.base.device, struct panvk_device, vk);
}

static inline void
panvk_queue_finish(struct panvk_queue *queue)
{
   struct panvk_device *dev = panvk_queue_get_device(queue);

   vk_queue_finish(&queue->vk);
#if PAN_ARCH >= 10
   drmSyncobjDestroy(dev->vk.drm_fd, queue->sync.handle);
#else
   drmSyncobjDestroy(dev->vk.drm_fd, queue->sync);
#endif
}

VkResult panvk_per_arch(queue_init)(struct panvk_device *device,
                                    struct panvk_queue *queue, int idx,
                                    const VkDeviceQueueCreateInfo *create_info);

#endif

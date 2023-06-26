/*
 * Copyright © 2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "decode.h"

#include "panvk_cmd_buffer.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_event.h"
#include "panvk_instance.h"
#include "panvk_macros.h"
#include "panvk_physical_device.h"
#include "panvk_priv_bo.h"
#include "panvk_queue.h"

#include "vk_drm_syncobj.h"

static VkResult
panvk_queue_submit(struct vk_queue *vk_queue, struct vk_queue_submit *submit)
{
   panvk_stub();
   return VK_ERROR_UNKNOWN;
}

VkResult
panvk_per_arch(queue_init)(struct panvk_device *device,
                           struct panvk_queue *queue, int idx,
                           const VkDeviceQueueCreateInfo *create_info)
{
   VkResult result = vk_queue_init(&queue->vk, &device->vk, create_info, idx);
   if (result != VK_SUCCESS)
      return result;

   int ret = drmSyncobjCreate(device->vk.drm_fd, DRM_SYNCOBJ_CREATE_SIGNALED,
                              &queue->sync);
   if (ret) {
      vk_queue_finish(&queue->vk);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   queue->vk.driver_submit = panvk_queue_submit;
   return VK_SUCCESS;
}

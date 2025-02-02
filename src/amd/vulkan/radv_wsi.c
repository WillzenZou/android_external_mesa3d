/*
 * Copyright © 2016 Red Hat
 * based on intel anv code:
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

#include "radv_wsi.h"
#include "meta/radv_meta.h"
#include "util/macros.h"
#include "radv_debug.h"
#include "radv_private.h"
#include "vk_fence.h"
#include "vk_semaphore.h"
#include "vk_util.h"
#include "wsi_common.h"

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
radv_wsi_proc_addr(VkPhysicalDevice physicalDevice, const char *pName)
{
   RADV_FROM_HANDLE(radv_physical_device, pdev, physicalDevice);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);
   return vk_instance_get_proc_addr_unchecked(&instance->vk, pName);
}

static void
radv_wsi_set_memory_ownership(VkDevice _device, VkDeviceMemory _mem, VkBool32 ownership)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   RADV_FROM_HANDLE(radv_device_memory, mem, _mem);

   if (device->use_global_bo_list) {
      device->ws->buffer_make_resident(device->ws, mem->bo, ownership);
   }
}

static VkQueue
radv_wsi_get_prime_blit_queue(VkDevice _device)
{
   RADV_FROM_HANDLE(radv_device, device, _device);
   struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   if (device->private_sdma_queue != VK_NULL_HANDLE)
      return vk_queue_to_handle(&device->private_sdma_queue->vk);

   if (pdev->info.gfx_level >= GFX9 && !(instance->debug_flags & RADV_DEBUG_NO_DMA_BLIT)) {

      pdev->vk_queue_to_radv[pdev->num_queues++] = RADV_QUEUE_TRANSFER;
      const VkDeviceQueueCreateInfo queue_create = {
         .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
         .queueFamilyIndex = pdev->num_queues - 1,
         .queueCount = 1,
      };

      device->private_sdma_queue =
         vk_zalloc(&device->vk.alloc, sizeof(struct radv_queue), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

      VkResult result = radv_queue_init(device, device->private_sdma_queue, 0, &queue_create, NULL);
      if (result == VK_SUCCESS) {
         return vk_queue_to_handle(&device->private_sdma_queue->vk);
      } else {
         vk_free(&device->vk.alloc, device->private_sdma_queue);
         device->private_sdma_queue = VK_NULL_HANDLE;
      }
   }
   return VK_NULL_HANDLE;
}

VkResult
radv_init_wsi(struct radv_physical_device *pdev)
{
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   VkResult result =
      wsi_device_init(&pdev->wsi_device, radv_physical_device_to_handle(pdev), radv_wsi_proc_addr, &instance->vk.alloc,
                      pdev->master_fd, &instance->drirc.options, &(struct wsi_device_options){.sw_device = false});
   if (result != VK_SUCCESS)
      return result;

   pdev->wsi_device.supports_modifiers = pdev->info.gfx_level >= GFX9;
   pdev->wsi_device.set_memory_ownership = radv_wsi_set_memory_ownership;
   pdev->wsi_device.get_blit_queue = radv_wsi_get_prime_blit_queue;

   wsi_device_setup_syncobj_fd(&pdev->wsi_device, pdev->local_fd);

   pdev->vk.wsi_device = &pdev->wsi_device;

   return VK_SUCCESS;
}

void
radv_finish_wsi(struct radv_physical_device *pdev)
{
   const struct radv_instance *instance = radv_physical_device_instance(pdev);

   pdev->vk.wsi_device = NULL;
   wsi_device_finish(&pdev->wsi_device, &instance->vk.alloc);
}

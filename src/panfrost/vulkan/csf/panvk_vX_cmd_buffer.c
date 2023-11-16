/*
 * Copyright © 2023 Collabora Ltd.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "genxml/gen_macros.h"

#include "panvk_private.h"

#include "util/rounding.h"
#include "util/u_pack_color.h"
#include "vk_format.h"

static uint32_t
panvk_debug_adjust_bo_flags(const struct panvk_device *device,
                            uint32_t bo_flags)
{
   uint32_t debug_flags = device->physical_device->instance->debug_flags;

   if (debug_flags & PANVK_DEBUG_DUMP)
      bo_flags &= ~PAN_BO_INVISIBLE;

   return bo_flags;
}

static VkResult
panvk_create_cmdbuf(struct vk_command_pool *vk_pool,
                    struct vk_command_buffer **cmdbuf_out)
{
   struct panvk_device *device =
      container_of(vk_pool->base.device, struct panvk_device, vk);
   struct panvk_cmd_pool *pool =
      container_of(vk_pool, struct panvk_cmd_pool, vk);
   struct panvk_cmd_buffer *cmdbuf;

   cmdbuf = vk_zalloc(&device->vk.alloc, sizeof(*cmdbuf), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmdbuf)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_buffer_init(&pool->vk, &cmdbuf->vk,
                                            &panvk_per_arch(cmd_buffer_ops), 0);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, cmdbuf);
      return result;
   }

   cmdbuf->device = device;

   panvk_pool_init(&cmdbuf->desc_pool, &device->physical_device->pdev,
                   &pool->desc_bo_pool, 0, 64 * 1024,
                   "Command buffer descriptor pool", true);
   panvk_pool_init(&cmdbuf->tls_pool, &device->physical_device->pdev,
                   &pool->tls_bo_pool,
                   panvk_debug_adjust_bo_flags(device, PAN_BO_INVISIBLE),
                   64 * 1024, "TLS pool", false);
   panvk_pool_init(&cmdbuf->varying_pool, &device->physical_device->pdev,
                   &pool->varying_bo_pool,
                   panvk_debug_adjust_bo_flags(device, PAN_BO_INVISIBLE),
                   64 * 1024, "Varyings pool", false);
   list_inithead(&cmdbuf->batches);
   *cmdbuf_out = &cmdbuf->vk;
   return VK_SUCCESS;
}

static void
panvk_reset_cmdbuf(struct vk_command_buffer *vk_cmdbuf,
                   VkCommandBufferResetFlags flags)
{
   struct panvk_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_cmd_buffer, vk);

   vk_command_buffer_reset(&cmdbuf->vk);

   list_for_each_entry_safe(struct panvk_batch, batch, &cmdbuf->batches, node) {
      list_del(&batch->node);
      util_dynarray_fini(&batch->jobs);
      util_dynarray_fini(&batch->event_ops);

      vk_free(&cmdbuf->vk.pool->alloc, batch);
   }

   panvk_pool_reset(&cmdbuf->desc_pool);
   panvk_pool_reset(&cmdbuf->tls_pool);
   panvk_pool_reset(&cmdbuf->varying_pool);

   for (unsigned i = 0; i < MAX_BIND_POINTS; i++)
      memset(&cmdbuf->bind_points[i].desc_state.sets, 0,
             sizeof(cmdbuf->bind_points[0].desc_state.sets));
}

static void
panvk_destroy_cmdbuf(struct vk_command_buffer *vk_cmdbuf)
{
   struct panvk_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_cmd_buffer, vk);
   struct panvk_device *device = cmdbuf->device;

   list_for_each_entry_safe(struct panvk_batch, batch, &cmdbuf->batches, node) {
      list_del(&batch->node);
      util_dynarray_fini(&batch->jobs);
      util_dynarray_fini(&batch->event_ops);

      vk_free(&cmdbuf->vk.pool->alloc, batch);
   }

   panvk_pool_cleanup(&cmdbuf->desc_pool);
   panvk_pool_cleanup(&cmdbuf->tls_pool);
   panvk_pool_cleanup(&cmdbuf->varying_pool);
   vk_command_buffer_finish(&cmdbuf->vk);
   vk_free(&device->vk.alloc, cmdbuf);
}

const struct vk_command_buffer_ops panvk_per_arch(cmd_buffer_ops) = {
   .create = panvk_create_cmdbuf,
   .reset = panvk_reset_cmdbuf,
   .destroy = panvk_destroy_cmdbuf,
};

void
panvk_per_arch(CmdNextSubpass2)(VkCommandBuffer commandBuffer,
                                const VkSubpassBeginInfo *pSubpassBeginInfo,
                                const VkSubpassEndInfo *pSubpassEndInfo)
{
   panvk_stub();
}

void
panvk_per_arch(CmdNextSubpass)(VkCommandBuffer cmd, VkSubpassContents contents)
{
   panvk_stub();
}

void
panvk_per_arch(CmdDraw)(VkCommandBuffer commandBuffer, uint32_t vertexCount,
                        uint32_t instanceCount, uint32_t firstVertex,
                        uint32_t firstInstance)
{
   panvk_stub();
}

void
panvk_per_arch(CmdDrawIndexed)(VkCommandBuffer commandBuffer,
                               uint32_t indexCount, uint32_t instanceCount,
                               uint32_t firstIndex, int32_t vertexOffset,
                               uint32_t firstInstance)
{
   panvk_stub();
}

VkResult
panvk_per_arch(EndCommandBuffer)(VkCommandBuffer commandBuffer)
{
   panvk_stub();
   return VK_SUCCESS;
}

void
panvk_per_arch(CmdEndRenderPass2)(VkCommandBuffer commandBuffer,
                                  const VkSubpassEndInfo *pSubpassEndInfo)
{
   panvk_stub();
}

void
panvk_per_arch(CmdEndRenderPass)(VkCommandBuffer cmd)
{
   panvk_stub();
}

void
panvk_per_arch(CmdPipelineBarrier2)(VkCommandBuffer commandBuffer,
                                    const VkDependencyInfo *pDependencyInfo)
{
   panvk_stub();
}

void
panvk_per_arch(CmdSetEvent2)(VkCommandBuffer commandBuffer, VkEvent _event,
                             const VkDependencyInfo *pDependencyInfo)
{
   panvk_stub();
}

void
panvk_per_arch(CmdResetEvent2)(VkCommandBuffer commandBuffer, VkEvent _event,
                               VkPipelineStageFlags2 stageMask)
{
   panvk_stub();
}

void
panvk_per_arch(CmdWaitEvents2)(VkCommandBuffer commandBuffer,
                               uint32_t eventCount, const VkEvent *pEvents,
                               const VkDependencyInfo *pDependencyInfos)
{
   panvk_stub();
}

VkResult
panvk_per_arch(BeginCommandBuffer)(VkCommandBuffer commandBuffer,
                                   const VkCommandBufferBeginInfo *pBeginInfo)
{
   panvk_stub();
   return VK_SUCCESS;
}

void
panvk_per_arch(DestroyCommandPool)(VkDevice _device, VkCommandPool commandPool,
                                   const VkAllocationCallbacks *pAllocator)
{
   panvk_stub();
}


void
panvk_per_arch(CmdDispatch)(VkCommandBuffer commandBuffer, uint32_t x,
                            uint32_t y, uint32_t z)
{
   panvk_stub();
}

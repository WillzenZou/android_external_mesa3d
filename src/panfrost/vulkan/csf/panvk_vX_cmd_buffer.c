/*
 * Copyright © 2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "csf/panvk_vX_cmd_buffer.h"
#include "panvk_buffer.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_pool.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_event.h"
#include "panvk_instance.h"
#include "panvk_physical_device.h"
#include "panvk_pipeline.h"
#include "panvk_pipeline_layout.h"
#include "panvk_priv_bo.h"

#include "util/rounding.h"
#include "util/u_pack_color.h"
#include "vk_format.h"

static uint32_t
panvk_debug_adjust_bo_flags(const struct panvk_device *device,
                            uint32_t bo_flags)
{
   struct panvk_instance *instance =
      to_panvk_instance(device->vk.physical->instance);

   if (instance->debug_flags & PANVK_DEBUG_DUMP)
      bo_flags &= ~PAN_KMOD_BO_FLAG_NO_MMAP;

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
   struct panvk_csf_cmd_buffer *cmdbuf;

   cmdbuf = vk_zalloc(&device->vk.alloc, sizeof(*cmdbuf), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmdbuf)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_buffer_init(&pool->vk, &cmdbuf->base.vk,
                                            &panvk_per_arch(cmd_buffer_ops), 0);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, cmdbuf);
      return result;
   }

   panvk_pool_init(&cmdbuf->base.desc_pool, device, &pool->desc_bo_pool, 0,
                   64 * 1024, "Command buffer descriptor pool", true);
   panvk_pool_init(
      &cmdbuf->base.tls_pool, device, &pool->tls_bo_pool,
      panvk_debug_adjust_bo_flags(device, PAN_KMOD_BO_FLAG_NO_MMAP), 64 * 1024,
      "TLS pool", false);

   /* TODO: Set cmdbuf->tiler_desc */

   for (uint32_t i = 0; i < PANVK_CSF_QUEUE_COUNT; i++) {
      /* cs_builder_init(cmdbuf->streams[i].builder, const struct cs_builder_conf *conf,
       *                 struct cs_buffer root_buffer)
       */
   }

   *cmdbuf_out = &cmdbuf->base.vk;
   return VK_SUCCESS;
}

static void
panvk_reset_cmdbuf(struct vk_command_buffer *vk_cmdbuf,
                   VkCommandBufferResetFlags flags)
{
   struct panvk_csf_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_csf_cmd_buffer, base.vk);

   vk_command_buffer_reset(&cmdbuf->base.vk);

   panvk_pool_reset(&cmdbuf->base.desc_pool);
   panvk_pool_reset(&cmdbuf->base.tls_pool);

   for (unsigned i = 0; i < MAX_BIND_POINTS; i++)
      memset(&cmdbuf->base.bind_points[i].desc_state.sets, 0,
             sizeof(cmdbuf->base.bind_points[0].desc_state.sets));
}

static void
panvk_destroy_cmdbuf(struct vk_command_buffer *vk_cmdbuf)
{
   struct panvk_csf_cmd_buffer *cmdbuf =
      container_of(vk_cmdbuf, struct panvk_csf_cmd_buffer, base.vk);
   struct panvk_device *dev = to_panvk_device(cmdbuf->base.vk.base.device);

   panvk_pool_cleanup(&cmdbuf->base.desc_pool);
   panvk_pool_cleanup(&cmdbuf->base.tls_pool);
   vk_command_buffer_finish(&cmdbuf->base.vk);
   vk_free(&dev->vk.alloc, cmdbuf);
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

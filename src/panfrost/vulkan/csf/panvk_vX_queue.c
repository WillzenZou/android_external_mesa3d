/*
 * Copyright © 2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"
#include "genxml/cs_builder.h"

#include "decode.h"
#include "drm-uapi/panthor_drm.h"

#include "csf/panvk_vX_cmd_buffer.h"
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

#if PAN_ARCH < 10
#error "CSF helpers are only used for gen >= 10"
#endif

static void
panvk_init_panthor_group(const struct panvk_device *dev,
                         struct drm_panthor_group_create *gc)
{
   /* create 3 queues:
    * graphics = vertex/tiling queue + fragment queue,
    * compute  = compute queue,
    * transfer = re-use the compute or graphics queues for now
    */
   struct drm_panthor_queue_create qc[] = {
      {
         .priority = 1,
         .ringbuf_size = 64 * 1024,
      },
      {
         .priority = 1,
         .ringbuf_size = 64 * 1024,
      },
      {
         .priority = 1,
         .ringbuf_size = 64 * 1024,
      },
   };

   /* create panthor group */
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   uint64_t shader_present = phys_dev->kmod.props.shader_present;
   *gc = (struct drm_panthor_group_create){
      .compute_core_mask = shader_present,
      .fragment_core_mask = shader_present,
      .tiler_core_mask = 1,
      .max_compute_cores = util_bitcount64(shader_present),
      .max_fragment_cores = util_bitcount64(shader_present),
      .max_tiler_cores = 1,
      .priority = PANTHOR_GROUP_PRIORITY_MEDIUM,
      .queues = DRM_PANTHOR_OBJ_ARRAY(ARRAY_SIZE(qc), qc),
      .vm_id = pan_kmod_vm_handle(dev->kmod.vm),
   };

   int ret =
      drmIoctl(dev->vk.drm_fd, DRM_IOCTL_PANTHOR_GROUP_CREATE, &gc);

   assert(!ret);
}

static uint64_t
panvk_cs_gpu_addr(struct panvk_csf_cmd_buffer *cmdbuf, uint32_t pqueue_idx)
{
   return cmdbuf->streams[pqueue_idx].builder->root_chunk.buffer.gpu;
}

static uint32_t
panvk_cs_size(struct panvk_csf_cmd_buffer *cmdbuf, uint32_t pqueue_idx)
{
   return (cmdbuf->streams[pqueue_idx].builder->root_chunk.size) * 8;
}

static uint32_t
panvk_cs_flush_id(struct panvk_csf_cmd_buffer *cmdbuf, uint32_t pqueue_idx)
{
   return cmdbuf->streams[pqueue_idx].latest_flush_id;
}

static void
panvk_prepare_cmd_buffer_qsubmits(struct panvk_queue *queue,
                                  struct panvk_csf_cmd_buffer *cmdbuf,
                                  struct drm_panthor_queue_submit *qsubmits,
                                  uint32_t *qsubmit_count,
                                  struct drm_panthor_sync_op *syncs,
                                  uint32_t sync_count)
{
   uint32_t pqueue_idx;
   for(pqueue_idx = 0; pqueue_idx < queue->pqueue_count; pqueue_idx++) {
      if (panvk_cs_size(cmdbuf, pqueue_idx) == 0)
         continue;

      qsubmits[(*qsubmit_count)++] = (struct drm_panthor_queue_submit) {
         .queue_index = pqueue_idx,
         .stream_addr = panvk_cs_gpu_addr(cmdbuf, pqueue_idx),
         .stream_size = panvk_cs_size(cmdbuf, pqueue_idx),
         .latest_flush = panvk_cs_flush_id(cmdbuf, pqueue_idx),
         /* Should be prepared based on in/out VkFence/[Timeline]Semaphore() */
         .syncs = DRM_PANTHOR_OBJ_ARRAY(sync_count, syncs),
      };
   }
}

static enum drm_panthor_sync_op_flags
get_panthor_syncobj_flag(uint64_t sync_value)
{
   return sync_value ? DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ
                     : DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_SYNCOBJ;
}

static void
panvk_prepare_wait_syncs(struct vk_queue_submit *submit,
                         struct drm_panthor_sync_op *syncs,
                         struct panvk_queue *queue)
{
   syncs[0] = (struct drm_panthor_sync_op){
      .flags = DRM_PANTHOR_SYNC_OP_WAIT |
         get_panthor_syncobj_flag(queue->sync.point),
      .handle = queue->sync.handle,
      .timeline_value = queue->sync.point,
   };

   for (unsigned i = 1; i < submit->wait_count + 1; i++) {
      struct vk_sync_wait *wait = &submit->waits[i];
      assert(vk_sync_type_is_drm_syncobj(wait->sync->type));
      struct vk_drm_syncobj *syncobj = vk_sync_as_drm_syncobj(wait->sync);

      syncs[i] = (struct drm_panthor_sync_op){
         .flags = DRM_PANTHOR_SYNC_OP_WAIT |
            get_panthor_syncobj_flag(wait->wait_value),
         .handle = syncobj->syncobj,
         .timeline_value = wait->wait_value,
      };
   }
}

static void
panvk_prepare_signal_syncs(struct vk_queue_submit *submit,
                           struct drm_panthor_sync_op *syncs)
{
   for (unsigned i = 0; i < submit->signal_count; i++) {
      struct vk_sync_signal *signal = &submit->signals[i];
      assert(vk_sync_type_is_drm_syncobj(signal->sync->type));
      struct vk_drm_syncobj *syncobj = vk_sync_as_drm_syncobj(signal->sync);

      syncs[i] = (struct drm_panthor_sync_op){
         .flags = DRM_PANTHOR_SYNC_OP_SIGNAL |
            get_panthor_syncobj_flag(signal->signal_value),
         .handle = syncobj->syncobj,
         .timeline_value = signal->signal_value,
      };
   }
}

static void
panvk_queue_submit_gsubmit(struct panvk_queue *queue,
                           struct drm_panthor_queue_submit *qsubmits,
                           uint32_t qsubmit_count)
{
   struct panvk_device *dev = to_panvk_device(queue->vk.base.device);
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   struct panvk_instance *instance =
      to_panvk_instance(dev->vk.physical->instance);
   unsigned debug = instance->debug_flags;
   int ret;

   struct drm_panthor_group_create gcreate;
   panvk_init_panthor_group(dev, &gcreate);

   struct drm_panthor_group_submit gsubmit = {
      .group_handle = gcreate.group_handle,
      .queue_submits = DRM_PANTHOR_OBJ_ARRAY(qsubmit_count, qsubmits),
   };

   ret =
      drmIoctl(dev->vk.drm_fd, DRM_IOCTL_PANTHOR_GROUP_SUBMIT, &gsubmit);
   assert(!ret);

   if (debug & (PANVK_DEBUG_TRACE | PANVK_DEBUG_SYNC)) {
      ret = drmSyncobjWait(dev->vk.drm_fd, &queue->sync.handle, 1,
                           INT64_MAX, 0, NULL);
      assert(!ret);
   }

   if (debug & PANVK_DEBUG_TRACE) {
      for (unsigned i = 0; i < qsubmit_count; i++) {
         uint32_t regs[256] = {0};
         pandecode_cs(dev->debug.decode_ctx, qsubmits[i].stream_addr,
                      qsubmits[i].stream_size,
                      phys_dev->kmod.props.gpu_prod_id, regs);
      }
   }

   if (debug & PANVK_DEBUG_DUMP)
      pandecode_dump_mappings(dev->debug.decode_ctx);

}

static uint32_t
panvk_queue_count_cmd_buffer_qsubmits(struct vk_queue_submit *submit)
{
   uint32_t nonempty_count = 0;
   for (uint32_t i = 0; i < submit->command_buffer_count; i++) {
      struct panvk_csf_cmd_buffer *cmdbuf =
         container_of(submit->command_buffers[i],
                      struct panvk_csf_cmd_buffer, base.vk);

      for (uint32_t j = 0; j < PANVK_CSF_QUEUE_COUNT; j++) {
         if (cmdbuf->streams[j].builder->root_chunk.size != 0)
            nonempty_count++;
      }
   }

   return nonempty_count;
}

static VkResult
panvk_queue_submit(struct vk_queue *vk_queue, struct vk_queue_submit *submit)
{
   struct panvk_queue *queue = container_of(vk_queue,
                                            struct panvk_queue, vk);

   struct drm_panthor_sync_op *syncs = NULL;
   uint32_t sync_count = submit->wait_count + submit->signal_count + 1;
   syncs = calloc(sync_count, sizeof(*syncs));
   assert(syncs);

   panvk_prepare_wait_syncs(submit, syncs, queue);
   panvk_prepare_signal_syncs(submit, syncs + submit->wait_count + 1);

   struct drm_panthor_queue_submit *qsubmits = NULL;
   uint32_t qsubmit_count = 0;
   uint32_t max_qsubmits = panvk_queue_count_cmd_buffer_qsubmits(submit);
   qsubmits = calloc(max_qsubmits, sizeof(*qsubmits));
   assert(qsubmits);

   for (uint32_t i = 0; i < submit->command_buffer_count; i++) {
      struct panvk_csf_cmd_buffer *cmdbuf =
         container_of(submit->command_buffers[i],
                      struct panvk_csf_cmd_buffer, base.vk);

      for (uint32_t j = 0; j < PANVK_CSF_QUEUE_COUNT; j++) {
         if (cmdbuf->streams[j].builder->root_chunk.size != 0) {
            panvk_prepare_cmd_buffer_qsubmits(queue, cmdbuf,
                                              qsubmits, &qsubmit_count,
                                              syncs, sync_count);
         }
      }
   }

   panvk_queue_submit_gsubmit(queue, qsubmits, qsubmit_count);
   free(syncs);
   free(qsubmits);

   return VK_SUCCESS;
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
                              &queue->sync.handle);
   if (ret) {
      vk_queue_finish(&queue->vk);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   queue->vk.driver_submit = panvk_queue_submit;
   return VK_SUCCESS;
}

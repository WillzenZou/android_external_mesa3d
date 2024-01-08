/*
 * Copyright © 2023 Collabora, Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <xf86drm.h>

#include "util/hash_table.h"
#include "util/libsync.h"
#include "util/macros.h"
#include "util/os_time.h"
#include "util/u_debug.h"
#include "util/vma.h"

#include "drm-uapi/dma-buf.h"
#include "drm-uapi/panthor_drm.h"

#include "pan_kmod_backend.h"

const struct pan_kmod_ops panthor_kmod_ops;

struct panthor_kmod_async_unmap {
   struct list_head node;
   uint64_t sync_point;
   uint64_t va;
   size_t size;
};

struct panthor_kmod_vm {
   struct pan_kmod_vm base;
   struct util_vma_heap vma;
   struct list_head async_unmaps;
   struct {
      uint32_t handle;
      uint64_t point;
   } sync;
};

struct panthor_kmod_dev {
   struct pan_kmod_dev base;
   uint32_t *flush_id;
};

struct panthor_kmod_bo {
   struct pan_kmod_bo base;
   struct {
      uint32_t handle;
      uint64_t read_point;
      uint64_t write_point;
   } sync;
};

static struct pan_kmod_dev *
panthor_kmod_dev_create(int fd, drmVersionPtr version,
                        const struct pan_kmod_allocator *allocator)
{
   struct panthor_kmod_dev *panthor_dev =
      pan_kmod_alloc(allocator, sizeof(*panthor_dev));
   if (!panthor_dev)
      return NULL;

   bool disable_flush_id = debug_get_bool_option("PAN_SHIM_DISABLE_FLUSH_ID", false);

   /* Handling this in drm-shim is not easy without major changes in it */
   if (unlikely(disable_flush_id)) {
      panthor_dev->flush_id = os_mmap(0, getpagesize(), PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, 0, 0);
   } else {
      panthor_dev->flush_id = os_mmap(0, getpagesize(), PROT_READ, MAP_SHARED, fd,
                                    DRM_PANTHOR_USER_FLUSH_ID_MMIO_OFFSET);
   }

   if (panthor_dev->flush_id == MAP_FAILED)
      goto err_free_dev;

   pan_kmod_dev_init(&panthor_dev->base, fd, version, &panthor_kmod_ops,
                     allocator);
   return &panthor_dev->base;

err_free_dev:
   pan_kmod_free(allocator, panthor_dev);
   return NULL;
}

static void
panthor_kmod_dev_destroy(struct pan_kmod_dev *dev)
{
   struct panthor_kmod_dev *panthor_dev =
      container_of(dev, struct panthor_kmod_dev, base);

   os_munmap(panthor_dev->flush_id, getpagesize());
   pan_kmod_dev_cleanup(dev);
   pan_kmod_free(dev->allocator, panthor_dev);
}

static void
panthor_dev_query_props(struct pan_kmod_dev *dev,
                        struct pan_kmod_dev_props *props)
{
   struct drm_panthor_gpu_info gpu_info = {};
   struct drm_panthor_dev_query query = {
      .type = DRM_PANTHOR_DEV_QUERY_GPU_INFO,
      .size = sizeof(gpu_info),
      .pointer = (uint64_t)(uintptr_t)&gpu_info,
   };

   ASSERTED int ret = drmIoctl(dev->fd, DRM_IOCTL_PANTHOR_DEV_QUERY, &query);
   assert(!ret);

   *props = (struct pan_kmod_dev_props){
      .gpu_prod_id = gpu_info.gpu_id >> 16,
      .gpu_revision = gpu_info.gpu_id & 0xffff,
      .shader_present = gpu_info.shader_present,
      .tiler_features = gpu_info.tiler_features,
      .mem_features = gpu_info.mem_features,
      .mmu_features = gpu_info.mmu_features,
      .thread_tls_alloc = 0,
      .afbc_features = 0,
   };

   static_assert(
      sizeof(props->texture_features) == sizeof(gpu_info.texture_features),
      "Mismatch in texture_features array size");

   memcpy(props->texture_features, gpu_info.texture_features,
          sizeof(props->texture_features));
}

static uint32_t
to_panthor_bo_flags(uint32_t flags)
{
   uint32_t panthor_flags = 0;

   if (flags & PAN_KMOD_BO_FLAG_NO_MMAP)
      panthor_flags |= DRM_PANTHOR_BO_NO_MMAP;

   return panthor_flags;
}

static struct pan_kmod_bo *
panthor_kmod_bo_alloc(struct pan_kmod_dev *dev,
                      struct pan_kmod_vm *exclusive_vm, size_t size,
                      uint32_t flags)
{
   /* We don't support allocating on-fault. */
   if (flags & PAN_KMOD_BO_FLAG_ALLOC_ON_FAULT)
      return NULL;

   struct panthor_kmod_vm *panthor_vm =
      exclusive_vm ? container_of(exclusive_vm, struct panthor_kmod_vm, base)
                   : NULL;
   struct panthor_kmod_bo *bo = pan_kmod_dev_alloc(dev, sizeof(*bo));
   if (!bo)
      return NULL;

   struct drm_panthor_bo_create req = {
      .size = size,
      .flags = to_panthor_bo_flags(flags),
      .exclusive_vm_id = panthor_vm ? panthor_vm->base.handle : 0,
   };

   int ret = drmIoctl(dev->fd, DRM_IOCTL_PANTHOR_BO_CREATE, &req);
   if (ret)
      goto err_free_bo;

   if (!exclusive_vm) {
      int ret = drmSyncobjCreate(dev->fd, DRM_SYNCOBJ_CREATE_SIGNALED,
                                 &bo->sync.handle);
      if (ret)
         goto err_destroy_bo;
   } else {
      bo->sync.handle = panthor_vm->sync.handle;
   }

   bo->sync.read_point = bo->sync.write_point = 0;

   pan_kmod_bo_init(&bo->base, dev, exclusive_vm, req.size, flags, req.handle);
   return &bo->base;

err_destroy_bo:
   drmCloseBufferHandle(dev->fd, bo->base.handle);
err_free_bo:
   pan_kmod_dev_free(dev, bo);
   return NULL;
}

static void
panthor_kmod_bo_free(struct pan_kmod_bo *bo)
{
   drmCloseBufferHandle(bo->dev->fd, bo->handle);
   pan_kmod_dev_free(bo->dev, bo);
}

static struct pan_kmod_bo *
panthor_kmod_bo_import(struct pan_kmod_dev *dev, int fd)
{
   struct panthor_kmod_bo *panthor_bo =
      pan_kmod_dev_alloc(dev, sizeof(*panthor_bo));
   if (!panthor_bo)
      return NULL;

   uint32_t handle;
   int ret = drmPrimeFDToHandle(dev->fd, fd, &handle);
   if (ret)
      goto err_free_bo;

   size_t size = lseek(fd, 0, SEEK_END);
   if (size == 0 || size == (size_t)-1)
      goto err_close_handle;

   ret = drmSyncobjCreate(dev->fd, 0, &panthor_bo->sync.handle);
   if (ret)
      goto err_close_handle;

   pan_kmod_bo_init(&panthor_bo->base, dev, NULL, size,
                    PAN_KMOD_BO_FLAG_IMPORTED, handle);
   return &panthor_bo->base;

err_close_handle:
   drmCloseBufferHandle(dev->fd, handle);

err_free_bo:
   pan_kmod_dev_free(dev, panthor_bo);
   return NULL;
}

static int
panthor_kmod_bo_export(struct pan_kmod_bo *bo)
{
   struct panthor_kmod_bo *panthor_bo =
      container_of(bo, struct panthor_kmod_bo, base);
   int dmabuf_fd;

   int ret =
      drmPrimeHandleToFD(bo->dev->fd, bo->handle, DRM_CLOEXEC, &dmabuf_fd);
   if (ret == -1)
      return -1;

   bool shared =
      bo->flags & (PAN_KMOD_BO_FLAG_EXPORTED | PAN_KMOD_BO_FLAG_IMPORTED);

   if (!shared) {
      if (panthor_bo->sync.read_point || panthor_bo->sync.write_point) {
         struct dma_buf_import_sync_file isync = {
            .flags = DMA_BUF_SYNC_RW,
         };
         int ret = drmSyncobjExportSyncFile(bo->dev->fd,
                                            panthor_bo->sync.handle, &isync.fd);
         assert(!ret);

         ret = drmIoctl(dmabuf_fd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &isync);
         assert(!ret);
         close(isync.fd);
      }

      /* Make sure we reset the syncobj on export. We will use it as a
       * temporary binary syncobj to import sync_file FD from now on.
       */
      ret = drmSyncobjReset(bo->dev->fd, &panthor_bo->sync.handle, 1);
      assert(!ret);
      panthor_bo->sync.read_point = 0;
      panthor_bo->sync.write_point = 0;
   }

   bo->flags |= PAN_KMOD_BO_FLAG_EXPORTED;
   return dmabuf_fd;
}

static off_t
panthor_kmod_bo_get_mmap_offset(struct pan_kmod_bo *bo)
{
   struct drm_panthor_bo_mmap_offset req = {.handle = bo->handle};
   ASSERTED int ret =
      drmIoctl(bo->dev->fd, DRM_IOCTL_PANTHOR_BO_MMAP_OFFSET, &req);

   assert(!ret);

   return req.offset;
}

static bool
panthor_kmod_bo_wait(struct pan_kmod_bo *bo, int64_t timeout_ns,
                     bool for_read_only_access)
{
   struct panthor_kmod_bo *panthor_bo =
      container_of(bo, struct panthor_kmod_bo, base);
   bool shared =
      bo->flags & (PAN_KMOD_BO_FLAG_EXPORTED | PAN_KMOD_BO_FLAG_IMPORTED);

   if (shared) {
      int dmabuf_fd;
      int ret =
         drmPrimeHandleToFD(bo->dev->fd, bo->handle, DRM_CLOEXEC, &dmabuf_fd);

      if (ret)
         return false;

      struct dma_buf_export_sync_file esync = {
         .flags = for_read_only_access ? DMA_BUF_SYNC_READ : DMA_BUF_SYNC_RW,
      };

      ret = drmIoctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &esync);
      close(dmabuf_fd);

      if (ret)
         return false;

      ret = sync_wait(esync.fd, timeout_ns / 1000000);
      close(esync.fd);
      return ret == 0;
   } else {
      uint64_t sync_point =
         for_read_only_access
            ? panthor_bo->sync.write_point
            : MAX2(panthor_bo->sync.write_point, panthor_bo->sync.read_point);

      if (!sync_point)
         return true;

      int64_t abs_timeout_ns = timeout_ns < INT64_MAX - os_time_get_nano()
                                  ? timeout_ns + os_time_get_nano()
                                  : INT64_MAX;
      int ret = drmSyncobjTimelineWait(bo->dev->fd, &panthor_bo->sync.handle,
                                       &sync_point, 1, abs_timeout_ns,
                                       DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL, NULL);
      if (ret >= 0)
         return true;

      assert(ret == -ETIME);
      return false;
   }
}

void
panthor_kmod_bo_attach_sync_point(struct pan_kmod_bo *bo, uint32_t sync_handle,
                                  uint64_t sync_point, bool read_only)
{
   struct panthor_kmod_bo *panthor_bo =
      container_of(bo, struct panthor_kmod_bo, base);
   struct panthor_kmod_vm *panthor_vm =
      bo->exclusive_vm
         ? container_of(bo->exclusive_vm, struct panthor_kmod_vm, base)
         : NULL;
   bool shared =
      bo->flags & (PAN_KMOD_BO_FLAG_EXPORTED | PAN_KMOD_BO_FLAG_IMPORTED);

   if (shared) {
      struct dma_buf_import_sync_file isync = {
         .flags = read_only ? DMA_BUF_SYNC_READ : DMA_BUF_SYNC_RW,
      };
      int dmabuf_fd;
      int ret = drmSyncobjExportSyncFile(bo->dev->fd, sync_handle, &isync.fd);
      assert(!ret);

      ret =
         drmPrimeHandleToFD(bo->dev->fd, bo->handle, DRM_CLOEXEC, &dmabuf_fd);
      assert(!ret);

      ret = drmIoctl(dmabuf_fd, DMA_BUF_IOCTL_IMPORT_SYNC_FILE, &isync);
      assert(!ret);
      close(dmabuf_fd);
      close(isync.fd);
   } else if (panthor_vm) {
      /* Private BOs should be passed the VM syncobj. */
      assert(sync_handle == panthor_vm->sync.handle);

      panthor_bo->sync.write_point =
         MAX2(sync_point, panthor_bo->sync.write_point);
      if (!read_only) {
         panthor_bo->sync.read_point =
            MAX2(sync_point, panthor_bo->sync.read_point);
      }
   } else {
      uint32_t new_sync_point =
         MAX2(panthor_bo->sync.write_point, panthor_bo->sync.read_point) + 1;

      int ret = drmSyncobjTransfer(bo->dev->fd, panthor_bo->sync.handle,
                                   new_sync_point, sync_handle, sync_point, 0);
      assert(!ret);

      panthor_bo->sync.write_point = new_sync_point;
      if (!read_only)
         panthor_bo->sync.read_point = new_sync_point;
   }
}

int
panthor_kmod_bo_get_sync_point(struct pan_kmod_bo *bo, uint32_t *sync_handle,
                               uint64_t *sync_point, bool for_read_only_access)
{
   struct panthor_kmod_bo *panthor_bo =
      container_of(bo, struct panthor_kmod_bo, base);
   bool shared =
      bo->flags & (PAN_KMOD_BO_FLAG_EXPORTED | PAN_KMOD_BO_FLAG_IMPORTED);

   if (shared) {
      int dmabuf_fd;
      int ret =
         drmPrimeHandleToFD(bo->dev->fd, bo->handle, DRM_CLOEXEC, &dmabuf_fd);

      if (ret) {
         debug_printf("drmPrimeHandleToFD() failed: %d\n", ret);
         return -1;
      }

      struct dma_buf_export_sync_file esync = {
         .flags = for_read_only_access ? DMA_BUF_SYNC_READ : DMA_BUF_SYNC_RW,
      };

      ret = drmIoctl(dmabuf_fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE, &esync);
      close(dmabuf_fd);
      if (ret) {
         debug_printf("drmIoctl(..., DMA_BUF_IOCTL_EXPORT_SYNC_FILE, ...) "
                      "failed: %d\n",
                      ret);
         return -1;
      }

      ret = drmSyncobjImportSyncFile(bo->dev->fd, panthor_bo->sync.handle,
                                     esync.fd);
      close(esync.fd);
      if (ret) {
         debug_printf("drmSyncobjImportSyncFile() failed: %d\n", ret);
         return -1;
      }

      *sync_handle = panthor_bo->sync.handle;
      *sync_point = 0;
   } else {
      *sync_handle = panthor_bo->sync.handle;
      *sync_point = for_read_only_access ? panthor_bo->sync.write_point
                                         : MAX2(panthor_bo->sync.read_point,
                                                panthor_bo->sync.write_point);
   }
   return 0;
}

static struct pan_kmod_vm *
panthor_kmod_vm_create(struct pan_kmod_dev *dev, uint32_t flags,
                       uint64_t user_va_start, uint64_t user_va_range)
{
   struct pan_kmod_dev_props props;

   panthor_dev_query_props(dev, &props);

   struct panthor_kmod_vm *panthor_vm =
      pan_kmod_dev_alloc(dev, sizeof(*panthor_vm));
   if (!panthor_vm)
      return NULL;

   list_inithead(&panthor_vm->async_unmaps);
   if (flags & PAN_KMOD_VM_FLAG_AUTO_VA)
      util_vma_heap_init(&panthor_vm->vma, user_va_start, user_va_range);

   panthor_vm->sync.point = 0;
   int ret = drmSyncobjCreate(dev->fd, DRM_SYNCOBJ_CREATE_SIGNALED,
                              &panthor_vm->sync.handle);
   if (ret)
      goto err_free_vm;

   uint64_t full_va_range = 1ull << DRM_PANTHOR_MMU_VA_BITS(props.mmu_features);
   struct drm_panthor_vm_create req = {
      .user_va_range = MIN2(full_va_range - user_va_start - user_va_range,
                              full_va_range >> 1),
   };

   ret = drmIoctl(dev->fd, DRM_IOCTL_PANTHOR_VM_CREATE, &req);
   if (ret)
      goto err_destroy_sync;

   pan_kmod_vm_init(&panthor_vm->base, dev, req.id, flags);
   return &panthor_vm->base;

err_destroy_sync:
   drmSyncobjDestroy(dev->fd, panthor_vm->sync.handle);

err_free_vm:
   if (flags & PAN_KMOD_VM_FLAG_AUTO_VA)
      util_vma_heap_finish(&panthor_vm->vma);

   pan_kmod_dev_free(dev, panthor_vm);
   return NULL;
}

static void
panthor_kmod_vm_collect_async_unmaps(struct panthor_kmod_vm *vm)
{
   bool done = false;

   list_for_each_entry_safe_rev(struct panthor_kmod_async_unmap, req,
                                &vm->async_unmaps, node)
   {
      if (!done) {
         int ret = drmSyncobjTimelineWait(
            vm->base.dev->fd, &vm->sync.handle, &req->sync_point, 1, 0,
            DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL, NULL);
         if (ret >= 0)
            done = true;
         else
            continue;
      }

      list_del(&req->node);
      util_vma_heap_free(&vm->vma, req->va, req->size);
      pan_kmod_dev_free(vm->base.dev, req);
   }
}

static void
panthor_kmod_vm_destroy(struct pan_kmod_vm *vm)
{
   struct panthor_kmod_vm *panthor_vm =
      container_of(vm, struct panthor_kmod_vm, base);
   struct drm_panthor_vm_destroy req = {.id = vm->handle};
   ASSERTED int ret = drmIoctl(vm->dev->fd, DRM_IOCTL_PANTHOR_VM_DESTROY, &req);
   assert(!ret);

   drmSyncobjDestroy(vm->dev->fd, panthor_vm->sync.handle);

   if (panthor_vm->base.flags & PAN_KMOD_VM_FLAG_AUTO_VA) {
      list_for_each_entry_safe(struct panthor_kmod_async_unmap, req,
                               &panthor_vm->async_unmaps, node) {
         list_del(&req->node);
         util_vma_heap_free(&panthor_vm->vma, req->va, req->size);
         pan_kmod_dev_free(vm->dev, req);
      }
      util_vma_heap_finish(&panthor_vm->vma);
   }

   pan_kmod_dev_free(vm->dev, panthor_vm);
}

static uint64_t
panthor_kmod_vm_map(struct pan_kmod_vm *vm, struct pan_kmod_bo *bo, uint64_t va,
                    off_t offset, size_t size)
{
   struct panthor_kmod_vm *panthor_vm =
      container_of(vm, struct panthor_kmod_vm, base);

   if (vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA) {
      panthor_kmod_vm_collect_async_unmaps(panthor_vm);
      va = util_vma_heap_alloc(&panthor_vm->vma, size,
                               size > 0x200000 ? 0x200000 : 0x1000);
   }

   struct drm_panthor_vm_bind_op bind_op = {
      .flags = DRM_PANTHOR_VM_BIND_OP_TYPE_MAP,
      .bo_handle = bo->handle,
      .bo_offset = offset,
      .va = va,
      .size = size,
   };
   struct drm_panthor_vm_bind req = {
      .vm_id = vm->handle,
      .flags = 0,
      .ops = DRM_PANTHOR_OBJ_ARRAY(1, &bind_op),
   };

   if (bo->flags & PAN_KMOD_BO_FLAG_EXECUTABLE)
      bind_op.flags |= DRM_PANTHOR_VM_BIND_OP_MAP_READONLY;
   else
      bind_op.flags |= DRM_PANTHOR_VM_BIND_OP_MAP_NOEXEC;

   if (bo->flags & PAN_KMOD_BO_FLAG_GPU_UNCACHED)
      bind_op.flags |= DRM_PANTHOR_VM_BIND_OP_MAP_UNCACHED;

   int ret = drmIoctl(vm->dev->fd, DRM_IOCTL_PANTHOR_VM_BIND, &req);
   if (ret && (vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA)) {
      util_vma_heap_free(&panthor_vm->vma, va, size);
      va = PAN_KMOD_VM_MAP_FAILED;
   }

   assert(offset == 0);
   assert(size == bo->size);
   return va;
}

static void
panthor_kmod_vm_unmap(struct pan_kmod_vm *vm, uint64_t va, size_t size)
{
   struct panthor_kmod_vm *panthor_vm =
      container_of(vm, struct panthor_kmod_vm, base);

   struct drm_panthor_sync_op syncs[2] = {
      {
         .flags = DRM_PANTHOR_SYNC_OP_WAIT |
                  DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ,
         .handle = panthor_vm->sync.handle,
         .timeline_value = panthor_vm->sync.point,
      },
      {
         .flags = DRM_PANTHOR_SYNC_OP_SIGNAL |
                  DRM_PANTHOR_SYNC_OP_HANDLE_TYPE_TIMELINE_SYNCOBJ,
         .handle = panthor_vm->sync.handle,
         .timeline_value = ++panthor_vm->sync.point,
      },
   };
   struct drm_panthor_vm_bind_op bind_op = {
      .flags = DRM_PANTHOR_VM_BIND_OP_TYPE_UNMAP,
      .va = va,
      .size = size,
      .syncs = DRM_PANTHOR_OBJ_ARRAY(ARRAY_SIZE(syncs), syncs),
   };
   struct drm_panthor_vm_bind req = {
      .vm_id = vm->handle,
      .flags = DRM_PANTHOR_VM_BIND_ASYNC,
      .ops = DRM_PANTHOR_OBJ_ARRAY(1, &bind_op),
   };

   ASSERTED int ret = drmIoctl(vm->dev->fd, DRM_IOCTL_PANTHOR_VM_BIND, &req);
   assert(!ret);

   if (vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA) {
      struct panthor_kmod_async_unmap *req =
         pan_kmod_dev_alloc(vm->dev, sizeof(*req));

      assert(req);
      req->va = va;
      req->size = size;
      req->sync_point = panthor_vm->sync.point;
      list_addtail(&req->node, &panthor_vm->async_unmaps);
   }
}

void
panthor_kmod_vm_new_sync_point(struct pan_kmod_vm *vm, uint32_t *sync_handle,
                               uint64_t *sync_point)
{
   struct panthor_kmod_vm *panthor_vm =
      container_of(vm, struct panthor_kmod_vm, base);

   *sync_handle = panthor_vm->sync.handle;
   *sync_point = ++panthor_vm->sync.point;
}

uint32_t
panthor_kmod_get_flush_id(const struct pan_kmod_dev *dev)
{
   struct panthor_kmod_dev *panthor_dev =
      container_of(dev, struct panthor_kmod_dev, base);

   return *(panthor_dev->flush_id);
}

const struct pan_kmod_ops panthor_kmod_ops = {
   .dev_create = panthor_kmod_dev_create,
   .dev_destroy = panthor_kmod_dev_destroy,
   .dev_query_props = panthor_dev_query_props,
   .bo_alloc = panthor_kmod_bo_alloc,
   .bo_free = panthor_kmod_bo_free,
   .bo_import = panthor_kmod_bo_import,
   .bo_export = panthor_kmod_bo_export,
   .bo_get_mmap_offset = panthor_kmod_bo_get_mmap_offset,
   .bo_wait = panthor_kmod_bo_wait,
   .vm_create = panthor_kmod_vm_create,
   .vm_destroy = panthor_kmod_vm_destroy,
   .vm_map = panthor_kmod_vm_map,
   .vm_unmap = panthor_kmod_vm_unmap,
};

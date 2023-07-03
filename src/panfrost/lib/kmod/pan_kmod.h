/*
 * Copyright © 2023 Collabora, Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <unistd.h>
#include <xf86drm.h>

#include "util/macros.h"
#include "util/os_file.h"
#include "util/os_mman.h"
#include "util/ralloc.h"

struct pan_kmod_dev;

enum pan_kmod_vm_flags {
   PAN_KMOD_VM_FLAG_AUTO_VA = BITFIELD_BIT(0),
};

struct pan_kmod_vm {
   uint32_t flags;
   uint32_t handle;
   struct pan_kmod_dev *dev;
};

enum pan_kmod_bo_flags {
   PAN_KMOD_BO_FLAG_EXECUTABLE = BITFIELD_BIT(0),
   PAN_KMOD_BO_FLAG_ALLOC_ON_FAULT = BITFIELD_BIT(1),
   PAN_KMOD_BO_FLAG_NO_MMAP = BITFIELD_BIT(2),
   PAN_KMOD_BO_FLAG_EXPORTED = BITFIELD_BIT(3),
   PAN_KMOD_BO_FLAG_IMPORTED = BITFIELD_BIT(4),
   PAN_KMOD_BO_FLAG_GPU_UNCACHED = BITFIELD_BIT(5),
};

struct pan_kmod_bo {
   size_t size;
   uint32_t handle;
   uint32_t flags;
   struct pan_kmod_vm *exclusive_vm;
   struct pan_kmod_dev *dev;
};

struct pan_kmod_dev_props {
   uint32_t gpu_prod_id;
   uint32_t gpu_revision;
   uint64_t shader_present;
   uint32_t tiler_features;
   uint32_t mem_features;
   uint32_t mmu_features;
   uint32_t texture_features[4];
   uint32_t thread_tls_alloc;
   uint32_t afbc_features;
};

struct pan_kmod_allocator {
   void *(*zalloc)(const struct pan_kmod_allocator *allocator, size_t size);
   void (*free)(const struct pan_kmod_allocator *allocator, void *data);
   void *priv;
};

#define PAN_KMOD_VM_MAP_AUTO_VA ~0ull
#define PAN_KMOD_VM_MAP_FAILED  ~0ull

struct pan_kmod_ops {
   struct pan_kmod_dev *(*dev_create)(
      int fd, const drmVersionPtr version,
      const struct pan_kmod_allocator *allocator);
   void (*dev_destroy)(struct pan_kmod_dev *dev);
   void (*dev_query_props)(struct pan_kmod_dev *dev,
                           struct pan_kmod_dev_props *props);
   struct pan_kmod_bo *(*bo_alloc)(struct pan_kmod_dev *dev,
                                   struct pan_kmod_vm *exclusive_vm,
                                   size_t size, uint32_t flags);
   void (*bo_free)(struct pan_kmod_bo *bo);
   struct pan_kmod_bo *(*bo_import)(struct pan_kmod_dev *dev, int fd);
   int (*bo_export)(struct pan_kmod_bo *bo);
   off_t (*bo_get_mmap_offset)(struct pan_kmod_bo *bo);
   bool (*bo_wait)(struct pan_kmod_bo *bo, int64_t timeout_ns,
                   bool for_read_only_access);
   void (*bo_make_evictable)(struct pan_kmod_bo *bo);
   bool (*bo_make_unevictable)(struct pan_kmod_bo *bo);
   struct pan_kmod_vm *(*vm_create)(struct pan_kmod_dev *dev, uint32_t flags,
                                    uint64_t va_start, uint64_t va_range);
   void (*vm_destroy)(struct pan_kmod_vm *vm);
   uint64_t (*vm_map)(struct pan_kmod_vm *vm, struct pan_kmod_bo *bo,
                      uint64_t va, off_t offset, size_t size);
   void (*vm_unmap)(struct pan_kmod_vm *vm, uint64_t va, size_t size);
};

struct pan_kmod_driver {
   struct {
      uint32_t major;
      uint32_t minor;
   } version;
};

struct pan_kmod_dev {
   int fd;
   struct pan_kmod_driver driver;
   const struct pan_kmod_ops *ops;
   const struct pan_kmod_allocator *allocator;
};

struct pan_kmod_dev *
pan_kmod_dev_create(int fd, const struct pan_kmod_allocator *allocator);

void pan_kmod_dev_destroy(struct pan_kmod_dev *dev);

static inline void
pan_kmod_dev_query_props(struct pan_kmod_dev *dev,
                         struct pan_kmod_dev_props *props)
{
   dev->ops->dev_query_props(dev, props);
}

static inline struct pan_kmod_bo *
pan_kmod_bo_alloc(struct pan_kmod_dev *dev, struct pan_kmod_vm *exclusive_vm,
                  size_t size, uint32_t flags)
{
   return dev->ops->bo_alloc(dev, exclusive_vm, size, flags);
}

static inline void
pan_kmod_bo_free(struct pan_kmod_bo *bo)
{
   bo->dev->ops->bo_free(bo);
}

static inline struct pan_kmod_bo *
pan_kmod_bo_import(struct pan_kmod_dev *dev, int fd)
{
   return dev->ops->bo_import(dev, fd);
}

static inline int
pan_kmod_bo_export(struct pan_kmod_bo *bo)
{
   if (bo->exclusive_vm)
      return -1;

   return bo->dev->ops->bo_export(bo);
}

static inline bool
pan_kmod_bo_wait(struct pan_kmod_bo *bo, int64_t timeout_ns,
                 bool for_read_only_access)
{
   return bo->dev->ops->bo_wait(bo, timeout_ns, for_read_only_access);
}

static inline void
pan_kmod_bo_make_evictable(struct pan_kmod_bo *bo)
{
   if (bo->dev->ops->bo_make_evictable)
      bo->dev->ops->bo_make_evictable(bo);
}

static inline bool
pan_kmod_bo_make_unevictable(struct pan_kmod_bo *bo)
{
   if (bo->dev->ops->bo_make_unevictable)
      return bo->dev->ops->bo_make_unevictable(bo);

   return true;
}

static inline void *
pan_kmod_bo_mmap(struct pan_kmod_bo *bo, off_t bo_offset, size_t size, int prot,
                 int flags)
{
   off_t mmap_offset;

   if (bo_offset + size > bo->size)
      return MAP_FAILED;

   mmap_offset = bo->dev->ops->bo_get_mmap_offset(bo);
   if (mmap_offset < 0)
      return MAP_FAILED;

   return os_mmap(NULL, size, prot, flags, bo->dev->fd,
                  mmap_offset + bo_offset);
}

static inline struct pan_kmod_vm *
pan_kmod_vm_create(struct pan_kmod_dev *dev, uint32_t flags, uint64_t va_start,
                   uint64_t va_range)
{
   return dev->ops->vm_create(dev, flags, va_start, va_range);
}

static inline void
pan_kmod_vm_destroy(struct pan_kmod_vm *vm)
{
   vm->dev->ops->vm_destroy(vm);
}

static inline uint64_t
pan_kmod_vm_map(struct pan_kmod_vm *vm, struct pan_kmod_bo *bo, uint64_t va,
                off_t offset, size_t size)
{
   if (!!(vm->flags & PAN_KMOD_VM_FLAG_AUTO_VA) !=
       (va == PAN_KMOD_VM_MAP_AUTO_VA))
      return PAN_KMOD_VM_MAP_FAILED;

   return vm->dev->ops->vm_map(vm, bo, va, offset, size);
}

static inline void
pan_kmod_vm_unmap(struct pan_kmod_vm *vm, uint64_t va, size_t size)
{
   vm->dev->ops->vm_unmap(vm, va, size);
}

static inline uint32_t
pan_kmod_vm_handle(struct pan_kmod_vm *vm)
{
   return vm->handle;
}

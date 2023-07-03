/*
 * Copyright © 2023 Collabora, Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "pan_kmod.h"

static inline void
pan_kmod_dev_init(struct pan_kmod_dev *dev, int fd, drmVersionPtr version,
                  const struct pan_kmod_ops *ops,
                  const struct pan_kmod_allocator *allocator)
{
   dev->driver.version.major = version->version_major;
   dev->driver.version.minor = version->version_minor;
   dev->fd = fd;
   dev->ops = ops;
   dev->allocator = allocator;
}

static inline void
pan_kmod_dev_cleanup(struct pan_kmod_dev *dev)
{
   close(dev->fd);
}

static inline void *
pan_kmod_alloc(const struct pan_kmod_allocator *allocator, size_t size)
{
   return allocator->zalloc(allocator, size);
}

static inline void
pan_kmod_free(const struct pan_kmod_allocator *allocator, void *data)
{
   return allocator->free(allocator, data);
}

static inline void *
pan_kmod_dev_alloc(struct pan_kmod_dev *dev, size_t size)
{
   return pan_kmod_alloc(dev->allocator, size);
}

static inline void
pan_kmod_dev_free(const struct pan_kmod_dev *dev, void *data)
{
   return pan_kmod_free(dev->allocator, data);
}

static inline void
pan_kmod_bo_init(struct pan_kmod_bo *bo, struct pan_kmod_dev *dev,
                 struct pan_kmod_vm *exclusive_vm, size_t size, uint32_t flags,
                 uint32_t handle)
{
   bo->dev = dev;
   bo->exclusive_vm = exclusive_vm;
   bo->size = size;
   bo->flags = flags;
   bo->handle = handle;
}

static inline void
pan_kmod_vm_init(struct pan_kmod_vm *vm, struct pan_kmod_dev *dev,
                 uint32_t handle, uint32_t flags)
{
   vm->dev = dev;
   vm->handle = handle;
   vm->flags = flags;
}

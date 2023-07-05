/*
 * Copyright © 2023 Collabora, Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <xf86drm.h>

#include "util/macros.h"
#include "pan_kmod.h"

extern const struct pan_kmod_ops panfrost_kmod_ops;
extern const struct pan_kmod_ops panthor_kmod_ops;

static const struct {
   const char *name;
   const struct pan_kmod_ops *ops;
} drivers[] = {
   {"panfrost", &panfrost_kmod_ops},
   {"panthor", &panthor_kmod_ops},
};

static void *
default_zalloc(const struct pan_kmod_allocator *allocator, size_t size)
{
   return rzalloc_size(allocator, size);
}

static void
default_free(const struct pan_kmod_allocator *allocator, void *data)
{
   return ralloc_free(data);
}

static const struct pan_kmod_allocator *
create_default_allocator(void)
{
   struct pan_kmod_allocator *allocator =
      rzalloc(NULL, struct pan_kmod_allocator);

   if (allocator) {
      allocator->zalloc = default_zalloc;
      allocator->free = default_free;
   }

   return allocator;
}

struct pan_kmod_dev *
pan_kmod_dev_create(int fd, const struct pan_kmod_allocator *allocator)
{
   drmVersionPtr version = drmGetVersion(fd);

   if (!version)
      return NULL;

   if (!allocator) {
      allocator = create_default_allocator();
      if (!allocator)
         return NULL;
   }

   for (unsigned i = 0; i < ARRAY_SIZE(drivers); i++) {
      if (!strcmp(drivers[i].name, version->name)) {
         const struct pan_kmod_ops *ops = drivers[i].ops;
         struct pan_kmod_dev *dev;

         dev = ops->dev_create(fd, version, allocator);
         if (dev)
            return dev;
         break;
      }
   }

   if (allocator->zalloc == default_zalloc)
      ralloc_free((void *)allocator);

   return NULL;
}

void
pan_kmod_dev_destroy(struct pan_kmod_dev *dev)
{
   const struct pan_kmod_allocator *allocator = dev->allocator;

   dev->ops->dev_destroy(dev);

   if (allocator->zalloc == default_zalloc)
      ralloc_free((void *)allocator);
}

/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_ARCH
#error "no arch"
#endif

#ifndef PANVK_VX_DESCRIPTOR_SET_H
#define PANVK_VX_DESCRIPTOR_SET_H

#include <stdint.h>

#include "util/bitset.h"
#include "util/vma.h"

#include "vk_object.h"

#include "panvk_pipeline_layout.h"

#define PANVK_DESCRIPTOR_SIZE 32

struct panvk_priv_bo;
struct panvk_sysvals;
struct panvk_descriptor_set_layout;

struct panvk2_descriptor_set {
   struct vk_object_base base;
   struct panvk_descriptor_set_layout *layout;
   struct {
      uint64_t dev;
      void *host;
   } descs;

   struct {
      uint64_t dev_addr;
      uint64_t size;
   } dyn_bufs[MAX_DYNAMIC_BUFFERS];

   /* Includes adjustment for variable-sized descriptors */
   unsigned num_descs;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk2_descriptor_set, base, VkDescriptorSet,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET)

struct panvk2_descriptor_pool {
   struct vk_object_base base;
   struct panvk_priv_bo *desc_bo;
   struct util_vma_heap desc_heap;

   /* Initialize to ones */
   BITSET_WORD *free_sets;

   uint32_t max_sets;
   struct panvk2_descriptor_set *sets;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk2_descriptor_pool, base, VkDescriptorPool,
                               VK_OBJECT_TYPE_DESCRIPTOR_POOL)

#endif /* PANVK_VX_DESCRIPTOR_SET_H */

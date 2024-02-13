/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_ARCH
#error "no arch"
#endif

#ifndef PANVK_VX_DESCRIPTOR_SET_LAYOUT_H
#define PANVK_VX_DESCRIPTOR_SET_LAYOUT_H

#include <stdint.h>

#include "vk_descriptor_set_layout.h"

#include "genxml/gen_macros.h"

struct panvk2_descriptor_set_binding_layout {
   VkDescriptorType type;
   VkDescriptorBindingFlags flags;
   unsigned array_size;
   unsigned num_descs;
   unsigned num_dyn_bufs;
   unsigned desc_idx;
   unsigned dyn_buf_idx;
   struct mali_sampler_packed *immutable_samplers;
};

struct panvk2_descriptor_set_layout {
   struct vk_descriptor_set_layout vk;
   unsigned char sha1[20];
   unsigned num_descs;
   unsigned num_dyn_bufs;

   /* Number of bindings in this descriptor set */
   uint32_t binding_count;

   /* Bindings in this descriptor set */
   struct panvk2_descriptor_set_binding_layout *bindings;

   unsigned first_sampler_desc_idx;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk2_descriptor_set_layout, vk.base,
                               VkDescriptorSetLayout,
                               VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT)

static inline const struct panvk2_descriptor_set_layout *
vk_to_panvk2_descriptor_set_layout(const struct vk_descriptor_set_layout *layout)
{
   return container_of(layout, const struct panvk2_descriptor_set_layout, vk);
}

static inline const uint32_t
panvk2_get_desc_stride(VkDescriptorType type)
{
   if (type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
      return 2;
   else
      return 1;
}

#endif /* PANVK_VX_DESCRIPTOR_SET_LAYOUT_H */

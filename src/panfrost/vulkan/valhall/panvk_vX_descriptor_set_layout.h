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
#include "panvk_vX_driver_descriptor_set.h"

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

static inline uint32_t
panvk2_get_dyn_desc_index(
   const struct panvk2_descriptor_set_binding_layout *layout, uint32_t set,
   uint32_t elem)
{
   assert(layout->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
          layout->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);

   assert(set < MAX_SETS && layout->dyn_buf_idx + elem < MAX_DYNAMIC_BUFFERS);

   return panvk2_driver_descriptor_set_idx(dynamic_buffers[0]) +
          (set * MAX_DYNAMIC_BUFFERS) + layout->dyn_buf_idx + elem;
}

static inline uint32_t
panvk2_get_desc_index(const struct panvk2_descriptor_set_binding_layout *layout,
                      uint32_t elem, VkDescriptorType type)
{
   assert(layout->type == type ||
          (layout->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
           (type == VK_DESCRIPTOR_TYPE_SAMPLER ||
            type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)));

   assert(layout->type != VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC &&
          layout->type != VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);

   uint32_t desc_idx =
      layout->desc_idx + elem * panvk2_get_desc_stride(layout->type);

   /* In case of combined and if asking for a sampled image, we need to
    * increment the index as the texture desciptor is right after the sampler */
   if (layout->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER &&
       type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE)
      desc_idx++;

   return desc_idx;
}

#endif /* PANVK_VX_DESCRIPTOR_SET_LAYOUT_H */

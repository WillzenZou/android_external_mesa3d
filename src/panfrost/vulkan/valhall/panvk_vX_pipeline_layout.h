/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_ARCH
#error "no arch"
#endif

#ifndef PANVK_VX_PIPELINE_LAYOUT_H
#define PANVK_VX_PIPELINE_LAYOUT_H

#include <stdint.h>

#include "vk_pipeline_layout.h"

#define PANVK_MAX_DESCRIPTOR_SETS 16

struct panvk2_pipeline_layout {
   struct vk_pipeline_layout vk;
   unsigned char sha1[20];
   unsigned num_dyn_bufs;

   struct {
      uint32_t size;
   } push_constants;

   struct {
      unsigned dyn_buf_offset;
   } sets[PANVK_MAX_DESCRIPTOR_SETS];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk2_pipeline_layout, vk.base,
                               VkPipelineLayout,
                               VK_OBJECT_TYPE_PIPELINE_LAYOUT)

#endif /* PANVK_VX_PIPELINE_LAYOUT_H */

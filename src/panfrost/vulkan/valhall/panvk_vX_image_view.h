/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_ARCH
#error "no arch"
#endif

#ifndef PANVK_VX_IMAGE_VIEW_H
#define PANVK_VX_IMAGE_VIEW_H

#include <stdint.h>

#include "vulkan/runtime/vk_image.h"

#include "pan_texture.h"
#include "genxml/gen_macros.h"

struct panvk_priv_bo;
struct panvk_image;

struct panvk2_image_view {
   struct vk_image_view vk;

   struct pan_image_view pview;

   struct panvk_priv_bo *bo;
   uint8_t plane_count;
   struct mali_texture_packed desc;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk2_image_view, vk.base, VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW);

#endif /* PANVK_VX_IMAGE_VIEW_H */

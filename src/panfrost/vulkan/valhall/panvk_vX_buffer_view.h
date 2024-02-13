/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_ARCH
#error "no arch"
#endif

#ifndef PANVK_VX_BUFFER_VIEW_H
#define PANVK_VX_BUFFER_VIEW_H

#include <stdint.h>

#include "vulkan/runtime/vk_buffer_view.h"

#include "genxml/gen_macros.h"

struct panvk_priv_bo;

struct panvk2_buffer_view {
   struct vk_buffer_view vk;
   struct panvk_priv_bo *planes_bo;
   struct mali_texture_packed desc;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk2_buffer_view, vk.base, VkBufferView,
                               VK_OBJECT_TYPE_BUFFER_VIEW)

#endif /* PANVK_VX_BUFFER_VIEW_H */

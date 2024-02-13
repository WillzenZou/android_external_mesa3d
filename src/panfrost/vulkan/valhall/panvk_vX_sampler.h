/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_ARCH
#error "no arch"
#endif

#ifndef PANVK_VX_SAMPLER_H
#define PANVK_VX_SAMPLER_H

#include <stdint.h>

#include "vulkan/runtime/vk_sampler.h"

#include "genxml/gen_macros.h"

struct panvk2_sampler {
   struct vk_sampler vk;
   struct mali_sampler_packed desc;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk2_sampler, vk.base, VkSampler,
                               VK_OBJECT_TYPE_SAMPLER)

#endif /* PANVK_VX_SAMPLER_H */

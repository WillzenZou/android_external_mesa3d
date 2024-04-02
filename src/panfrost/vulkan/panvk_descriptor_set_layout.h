/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_DESCRIPTOR_SET_LAYOUT_H
#define PANVK_DESCRIPTOR_SET_LAYOUT_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "vk_descriptor_set_layout.h"

#if PAN_ARCH >= 9
#include "valhall/panvk_vX_descriptor_set_layout.h"
#else
#include "bifrost/panvk_vX_descriptor_set_layout.h"
#endif

static inline const struct panvk_descriptor_set_layout *
vk_to_panvk_descriptor_set_layout(const struct vk_descriptor_set_layout *layout)
{
   return container_of(layout, const struct panvk_descriptor_set_layout, vk);
}

#endif

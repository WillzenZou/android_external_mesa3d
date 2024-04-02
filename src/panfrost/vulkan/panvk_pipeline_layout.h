/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_PIPELINE_LAYOUT_H
#define PANVK_PIPELINE_LAYOUT_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "vk_pipeline_layout.h"

#include "panvk_macros.h"

#define MAX_DYNAMIC_UNIFORM_BUFFERS 16
#define MAX_DYNAMIC_STORAGE_BUFFERS 8
#define MAX_DYNAMIC_BUFFERS                                                    \
   (MAX_DYNAMIC_UNIFORM_BUFFERS + MAX_DYNAMIC_STORAGE_BUFFERS)

#if PAN_ARCH >= 9
#include "valhall/panvk_vX_pipeline_layout.h"
#else
#include "bifrost/panvk_vX_pipeline_layout.h"
#endif

unsigned panvk_per_arch(pipeline_layout_ubo_start)(
   const struct panvk_pipeline_layout *layout, unsigned set, bool is_dynamic);

unsigned panvk_per_arch(pipeline_layout_ubo_index)(
   const struct panvk_pipeline_layout *layout, unsigned set, unsigned binding,
   unsigned array_index);

#endif

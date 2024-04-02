/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_DESCRIPTOR_SET_H
#define PANVK_DESCRIPTOR_SET_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "vk_object.h"

#if PAN_ARCH >= 9
#include "valhall/panvk_vX_descriptor_set.h"
#else
#include "bifrost/panvk_vX_descriptor_set.h"
#endif

/* This has to match nir_address_format_64bit_bounded_global */
struct panvk_ssbo_addr {
   uint64_t base_addr;
   uint32_t size;
   uint32_t zero; /* Must be zero! */
};

struct panvk_bview_desc {
   uint32_t elems;
};

struct panvk_image_desc {
   uint16_t width;
   uint16_t height;
   uint16_t depth;
   uint8_t levels;
   uint8_t samples;
};

struct panvk_buffer_desc {
   struct panvk_buffer *buffer;
   VkDeviceSize offset;
   VkDeviceSize size;
};

#endif

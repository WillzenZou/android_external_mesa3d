/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_VX_DRIVER_DESCRIPTOR_SET_H
#define PANVK_VX_DRIVER_DESCRIPTOR_SET_H

#include <stddef.h>
#include "genxml/gen_macros.h"
#include "panvk_cmd_buffer.h"
#include "panvk_pipeline_layout.h"

#define PANVK_DRIVER_DESC_SET 15
#define MAX_VERTEX_ATTRIBS    16

/**
 * Driver descriptor set.
 *
 * This is used to store any extra descriptor needed by panvk (like dynamic
 * buffers or vertex attrib)
 */
struct panvk2_driver_descriptor_set {
   struct mali_buffer_packed dynamic_buffers[MAX_SETS * MAX_DYNAMIC_BUFFERS];
   struct mali_buffer_packed vertex_buffers[MAX_VBS];
   struct mali_attribute_packed vertex_attribs[MAX_VERTEX_ATTRIBS];
};

#define panvk2_driver_descriptor_set_offset(member)                            \
   offsetof(struct panvk2_driver_descriptor_set, member)

#define panvk2_driver_descriptor_set_idx(member)                               \
   panvk2_driver_descriptor_set_offset(member) / 32

#endif /* PANVK_VX_DRIVER_DESCRIPTOR_SET_H */
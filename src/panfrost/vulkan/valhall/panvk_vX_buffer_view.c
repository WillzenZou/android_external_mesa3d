/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "drm-uapi/drm_fourcc.h"
#include "util/u_atomic.h"
#include "util/u_debug.h"
#include "vk_format.h"
#include "vk_object.h"
#include "vk_util.h"

#include "genxml/gen_macros.h"
#include "panvk_private.h"
#include "valhall/panvk_vX_buffer_view.h"

VkResult
panvk_per_arch(CreateBufferView)(VkDevice _device,
                                 const VkBufferViewCreateInfo *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 VkBufferView *pView)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_buffer, buffer, pCreateInfo->buffer);

   struct panvk2_buffer_view *view = vk_object_zalloc(
      &device->vk, pAllocator, sizeof(*view), VK_OBJECT_TYPE_BUFFER_VIEW);

   if (!view)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_buffer_view_init(&device->vk, &view->vk, pCreateInfo);

   mali_ptr address = panvk_buffer_gpu_ptr(buffer, pCreateInfo->offset);
   VkBufferUsageFlags tex_usage_mask = VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                                       VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;

   assert(!(address & 63));

   if (buffer->vk.usage & tex_usage_mask) {
      enum pipe_format pfmt = vk_format_to_pipe_format(view->vk.format);

      struct pan_image plane = {
         .data = {
            .base = address,
            .offset = 0,
	 },
         .layout = {
            .modifier = DRM_FORMAT_MOD_LINEAR,
            .format = pfmt,
            .dim = MALI_TEXTURE_DIMENSION_1D,
            .width = view->vk.elements,
            .height = 1,
            .depth = 1,
            .array_size = 1,
            .nr_samples = 1,
            .nr_slices = 1,
         },
      };

      struct pan_image_view pview = {
         .planes[0] = &plane,
         .format = pfmt,
         .dim = MALI_TEXTURE_DIMENSION_1D,
         .nr_samples = 1,
         .first_level = 0,
         .last_level = 0,
         .first_layer = 0,
         .last_layer = 0,
         .swizzle =
            {
               PIPE_SWIZZLE_X,
               PIPE_SWIZZLE_Y,
               PIPE_SWIZZLE_Z,
               PIPE_SWIZZLE_W,
            },
      };

      unsigned arch =
         pan_arch(device->physical_device->kmod.props.gpu_prod_id);
      pan_image_layout_init(arch, &plane.layout, NULL);

      unsigned bo_size =
         GENX(panfrost_estimate_texture_payload_size)(&pview);

      view->planes_bo =
         panvk_priv_bo_create(device, bo_size, 0, pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

      struct panfrost_ptr ptr = {
         .gpu = view->planes_bo->addr.dev,
         .cpu = view->planes_bo->addr.host,
      };

      GENX(panfrost_new_texture)(&pview, &view->desc.opaque, &ptr);
   }

   *pView = panvk2_buffer_view_to_handle(view);
   return VK_SUCCESS;
}

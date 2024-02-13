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
#include "valhall/panvk_vX_image_view.h"

static enum mali_texture_dimension
panvk2_view_type_to_mali_tex_dim(VkImageViewType type)
{
   switch (type) {
   case VK_IMAGE_VIEW_TYPE_1D:
   case VK_IMAGE_VIEW_TYPE_1D_ARRAY:
      return MALI_TEXTURE_DIMENSION_1D;
   case VK_IMAGE_VIEW_TYPE_2D:
   case VK_IMAGE_VIEW_TYPE_2D_ARRAY:
      return MALI_TEXTURE_DIMENSION_2D;
   case VK_IMAGE_VIEW_TYPE_3D:
      return MALI_TEXTURE_DIMENSION_3D;
   case VK_IMAGE_VIEW_TYPE_CUBE:
   case VK_IMAGE_VIEW_TYPE_CUBE_ARRAY:
      return MALI_TEXTURE_DIMENSION_CUBE;
   default:
      unreachable("Invalid view type");
   }
}

VkResult
panvk_per_arch(CreateImageView)(VkDevice _device,
                                const VkImageViewCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkImageView *pView)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_image, image, pCreateInfo->image);
   struct panvk2_image_view *view;

   view = vk_image_view_create(&device->vk, false, pCreateInfo, pAllocator,
                               sizeof(*view));
   if (view == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   view->pview = (struct pan_image_view){
      .planes[0] = &image->pimage,
      .format = vk_format_to_pipe_format(view->vk.view_format),
      .dim = panvk2_view_type_to_mali_tex_dim(view->vk.view_type),
      .nr_samples = image->vk.samples,
      .first_level = view->vk.base_mip_level,
      .last_level = view->vk.base_mip_level + view->vk.level_count - 1,
      .first_layer = view->vk.base_array_layer,
      .last_layer = view->vk.base_array_layer + view->vk.layer_count - 1,
   };
   vk_component_mapping_to_pipe_swizzle(view->vk.swizzle, view->pview.swizzle);

   /* Figure out which image planes we need. */
   view->plane_count = vk_format_get_plane_count(view->vk.format);

   if (view->vk.usage & VK_IMAGE_USAGE_STORAGE_BIT) {
      /* For storage images, we can't have any cubes */
      if (view->vk.view_type == VK_IMAGE_VIEW_TYPE_CUBE ||
          view->vk.view_type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY)
         view->pview.dim = MALI_TEXTURE_DIMENSION_2D;

      if (view->pview.dim == MALI_TEXTURE_DIMENSION_3D) {
         assert(view->vk.base_array_layer == 0);
         assert(view->vk.layer_count = 1);
      }
   }

   unsigned bo_size =
      GENX(panfrost_estimate_texture_payload_size)(&view->pview);

   view->bo = panvk_priv_bo_create(device, bo_size, 0, pAllocator,
                                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   STATIC_ASSERT(sizeof(view->desc) >= pan_size(TEXTURE));

   struct panfrost_ptr ptr = {
      .gpu = view->bo->addr.dev,
      .cpu = view->bo->addr.host,
   };

   GENX(panfrost_new_texture)(&view->pview, &view->desc, &ptr);

   *pView = panvk2_image_view_to_handle(view);
   return VK_SUCCESS;
}

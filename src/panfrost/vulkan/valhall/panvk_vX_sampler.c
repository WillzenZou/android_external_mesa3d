/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"
#include "panvk_private.h"
#include "valhall/panvk_vX_sampler.h"

VkResult
panvk_per_arch(CreateSampler)(VkDevice _device,
                              const VkSamplerCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator,
                              VkSampler *pSampler)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk2_sampler *sampler;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

   sampler = vk_object_alloc(&device->vk, pAllocator, sizeof(*sampler),
                             VK_OBJECT_TYPE_SAMPLER);
   if (!sampler)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   STATIC_ASSERT(sizeof(sampler->desc) >= pan_size(SAMPLER));

   pan_pack(&sampler->desc, SAMPLER, cfg) {
      /* TODO: determine what values need to go here */
/*      cfg.reduction_mode = ; */
/*      cfg.wrap_mode_r = ; */
/*      cfg.wrap_mode_t = ; */
/*      cfg.wrap_mode_s = ; */
/*      cfg.round_to_nearest_even = ; */
/*      cfg.srgb_override = ; */
/*      cfg.seamless_cube_map = ; */
/*      cfg.clamp_integer_coordinates = ; */
/*      cfg.normalized_coordinates = ; */
/*      cfg.clamp_integer_array_indices = ; */
/*      cfg.minify_nearest = ; */
/*      cfg.magnify_nearest = ; */
/*      cfg.magnify_cutoff = ; */
/*      cfg.mipmap_mode = ; */
/*      cfg.minimum_lod = ; */
/*      cfg.compare_function = ; */
/*      cfg.maximum_lod = ; */
/*      cfg.lod_bias = ; */
/*      cfg.maximum_anisotropy = ; */
/*      cfg.lod_algorithm = ; */
/*      cfg.border_color_r = ; */
/*      cfg.border_color_g = ; */
/*      cfg.border_color_b = ; */
/*      cfg.border_color_a = ; */
   }

   *pSampler = panvk2_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

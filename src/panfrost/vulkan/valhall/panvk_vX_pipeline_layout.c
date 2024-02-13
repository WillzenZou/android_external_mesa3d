/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "util/mesa-sha1.h"
#include "vk_descriptor_update_template.h"
#include "vk_descriptors.h"
#include "vk_util.h"
#include "vk_format.h"

#include "util/bitset.h"

#include "genxml/gen_macros.h"

#include "panvk_private.h"
#include "panvk_cs.h"

#include "valhall/panvk_vX_descriptor_set_layout.h"
#include "valhall/panvk_vX_pipeline_layout.h"

VkResult
panvk_per_arch(CreatePipelineLayout)(
   VkDevice _device, const VkPipelineLayoutCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk2_pipeline_layout *playout;
   struct mesa_sha1 ctx;

   assert(pCreateInfo->setLayoutCount <= PANVK_MAX_DESCRIPTOR_SETS);

   playout =
      vk_pipeline_layout_zalloc(&device->vk, sizeof(*playout), pCreateInfo);
   if (playout == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   _mesa_sha1_init(&ctx);

   unsigned desc_idx = 0, dyn_buf_idx = 0;
   for (unsigned set = 0; set < pCreateInfo->setLayoutCount; set++) {
      const struct panvk2_descriptor_set_layout *set_layout =
         vk_to_panvk2_descriptor_set_layout(playout->vk.set_layouts[set]);

      desc_idx += set_layout->num_descs;
      playout->sets[set].dyn_buf_offset = dyn_buf_idx;
      dyn_buf_idx += set_layout->num_dyn_bufs;

      for (unsigned b = 0; b < set_layout->binding_count; b++) {
         const struct panvk2_descriptor_set_binding_layout *binding_layout =
            &set_layout->bindings[b];
         _mesa_sha1_update(&ctx, &binding_layout->type,
                           sizeof(binding_layout->type));
         _mesa_sha1_update(&ctx, &binding_layout->array_size,
                           sizeof(binding_layout->array_size));
      }
   }

   for (unsigned range = 0; range < pCreateInfo->pushConstantRangeCount;
        range++) {
      playout->push_constants.size =
         MAX2(pCreateInfo->pPushConstantRanges[range].offset +
                 pCreateInfo->pPushConstantRanges[range].size,
              playout->push_constants.size);
   }

   playout->num_dyn_bufs = dyn_buf_idx;

   _mesa_sha1_final(&ctx, playout->sha1);

   *pPipelineLayout = panvk2_pipeline_layout_to_handle(playout);
   return VK_SUCCESS;
}

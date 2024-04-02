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
#include "vk_alloc.h"
#include "vk_descriptor_update_template.h"
#include "vk_descriptors.h"
#include "vk_util.h"
#include "vk_format.h"
#include "vk_log.h"

#include "util/bitset.h"

#include "genxml/gen_macros.h"

#include "panvk_descriptor_set_layout.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_macros.h"
#include "panvk_pipeline_layout.h"
#include "panvk_sampler.h"

#define PANVK_MAX_DESCS_PER_SET   (1 << 24)

static bool
binding_has_immutable_samplers(const VkDescriptorSetLayoutBinding *binding)
{
   switch (binding->descriptorType) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      return binding->pImmutableSamplers != NULL;

   default:
      return false;
   }
}

static void
binding_get_desc_count(const VkDescriptorSetLayoutBinding *binding,
                       unsigned *desc_count, unsigned *dyn_ubo_count,
                       unsigned *dyn_ssbo_count)
{
   switch (binding->descriptorType) {
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      *desc_count += binding->descriptorCount * 2;
      break;
   case VK_DESCRIPTOR_TYPE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      *desc_count += binding->descriptorCount;
      break;
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      *dyn_ubo_count += binding->descriptorCount;
      break;
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      *dyn_ssbo_count += binding->descriptorCount;
      break;
   default:
      unreachable("Invalid descriptor type");
   }
}

static bool
is_sampler(const VkDescriptorSetLayoutBinding *binding)
{
   switch (binding->descriptorType) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      return true;
   default:
      return false;
   }
}

static bool
is_texture(const VkDescriptorSetLayoutBinding *binding)
{
   switch (binding->descriptorType) {
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
      return true;
   default:
      return false;
   }
}

VkResult
panvk_per_arch(CreateDescriptorSetLayout)(
   VkDevice _device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VkDescriptorSetLayoutBinding *bindings = NULL;
   unsigned num_bindings = 0;
   VkResult result;

   unsigned immutable_sampler_count = 0;
   bool has_texture_desc = false, has_sampler_desc = false;
   for (uint32_t j = 0; j < pCreateInfo->bindingCount; j++) {
      const VkDescriptorSetLayoutBinding *binding = &pCreateInfo->pBindings[j];
      num_bindings = MAX2(num_bindings, binding->binding + 1);

      /* From the Vulkan 1.1.97 spec for VkDescriptorSetLayoutBinding:
       *
       *    "If descriptorType specifies a VK_DESCRIPTOR_TYPE_SAMPLER or
       *    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER type descriptor, then
       *    pImmutableSamplers can be used to initialize a set of immutable
       *    samplers. [...]  If descriptorType is not one of these descriptor
       *    types, then pImmutableSamplers is ignored.
       *
       * We need to be careful here and only parse pImmutableSamplers if we
       * have one of the right descriptor types.
       */
      if (binding_has_immutable_samplers(binding))
         immutable_sampler_count += binding->descriptorCount;

      has_sampler_desc = has_sampler_desc || is_sampler(binding);
      has_texture_desc = has_texture_desc || is_texture(binding);
   }

   if (pCreateInfo->bindingCount) {
      result = vk_create_sorted_bindings(pCreateInfo->pBindings,
                                         pCreateInfo->bindingCount, &bindings);
      if (result != VK_SUCCESS)
         return vk_error(device, result);

      num_bindings = bindings[pCreateInfo->bindingCount - 1].binding + 1;
   }

   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct panvk_descriptor_set_layout, layout, 1);
   VK_MULTIALLOC_DECL(&ma, struct panvk_descriptor_set_binding_layout,
                      binding_layouts, num_bindings);
   VK_MULTIALLOC_DECL(&ma, struct mali_sampler_packed, samplers,
                      immutable_sampler_count);

   if (!vk_descriptor_set_layout_multizalloc(&device->vk, &ma)) {
      free(bindings);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   layout->bindings = binding_layouts;
   layout->binding_count = num_bindings;

   const VkDescriptorSetLayoutBindingFlagsCreateInfo *binding_flags_info =
      vk_find_struct_const(pCreateInfo->pNext,
                           DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO);

   unsigned desc_idx = 0;
   if (has_texture_desc && !has_sampler_desc) {
      /* If needed, insert dummy sampler as first descriptor. */
      layout->first_sampler_desc_idx = desc_idx;
      desc_idx++;
   } else {
      /* If dummy sampler is not needed, set index to max
       * to indicate that it has not yet been assigned.
       */
      layout->first_sampler_desc_idx = PANVK_MAX_DESCS_PER_SET;
   }

   unsigned dyn_buf_idx = 0;
   for (unsigned i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding = &bindings[i];
      struct panvk_descriptor_set_binding_layout *binding_layout =
         &layout->bindings[binding->binding];

      if (binding->descriptorCount == 0)
         continue;

      binding_layout->type = binding->descriptorType;

      if (binding_flags_info && binding_flags_info->bindingCount > 0) {
         assert(binding_flags_info->bindingCount == pCreateInfo->bindingCount);
         binding_layout->flags = binding_flags_info->pBindingFlags[i];
      }

      binding_layout->array_size = binding->descriptorCount;

      if (binding_has_immutable_samplers(binding)) {
         binding_layout->immutable_samplers = samplers;
         samplers += binding->descriptorCount;
         for (uint32_t j = 0; i < binding->descriptorCount; j++) {
            VK_FROM_HANDLE(panvk_sampler, sampler, binding->pImmutableSamplers[j]);
            binding_layout->immutable_samplers[j] = sampler->desc;
         }
      }

      unsigned desc_count = 0, dyn_ubo_count = 0, dyn_ssbo_count = 0;
      binding_get_desc_count(binding, &desc_count, &dyn_ubo_count,
                             &dyn_ssbo_count);
      binding_layout->desc_idx = desc_idx;
      binding_layout->dyn_buf_idx = dyn_buf_idx;
      binding_layout->num_descs = desc_count;

      /* Ensure that binding_layout->num_descs accounts for dummy sampler.
       * Must be done before assigning first_sampler_desc_idx in this loop so
       * that first_sampler_desc_idx == 0 only if there is a dummy sampler.
       * */
      if (i == 0 && layout->first_sampler_desc_idx == 0)
         binding_layout->num_descs++;

      if (is_sampler(binding) &&
          layout->first_sampler_desc_idx == PANVK_MAX_DESCS_PER_SET)
         layout->first_sampler_desc_idx = desc_idx;

      binding_layout->num_dyn_bufs = dyn_ubo_count + dyn_ssbo_count;
      desc_idx += desc_count;
      dyn_buf_idx += dyn_ubo_count + dyn_ssbo_count;
   }

   layout->num_descs = desc_idx;
   layout->num_dyn_bufs = dyn_buf_idx;
   struct mesa_sha1 sha1_ctx;
   _mesa_sha1_init(&sha1_ctx);

   _mesa_sha1_update(&sha1_ctx, &layout->binding_count,
                     sizeof(layout->binding_count));
   _mesa_sha1_update(&sha1_ctx, &layout->num_descs,
                     sizeof(layout->num_descs));
   _mesa_sha1_update(&sha1_ctx, &layout->num_dyn_bufs,
                     sizeof(layout->num_dyn_bufs));

   for (uint32_t b = 0; b < num_bindings; b++) {
      _mesa_sha1_update(&sha1_ctx, &layout->bindings[b].type,
                        sizeof(layout->bindings[b].type));
      _mesa_sha1_update(&sha1_ctx, &layout->bindings[b].flags,
                        sizeof(layout->bindings[b].flags));
      _mesa_sha1_update(&sha1_ctx, &layout->bindings[b].array_size,
                        sizeof(layout->bindings[b].array_size));
      /* Immutable samplers are ignored for now */
   }

   _mesa_sha1_final(&sha1_ctx, layout->sha1);

   free(bindings);
   *pSetLayout = panvk_descriptor_set_layout_to_handle(layout);

   return VK_SUCCESS;
}

void
panvk_per_arch(GetDescriptorSetLayoutSupport)(
   VkDevice _device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   VkDescriptorSetLayoutSupport *pSupport)
{
   pSupport->supported = false;

   unsigned desc_count = 0, dyn_ubo_count = 0, dyn_ssbo_count = 0;
   bool has_texture_desc = false, has_sampler_desc = false;
   for (unsigned i = 0; i < pCreateInfo->bindingCount; i++) {
      const VkDescriptorSetLayoutBinding *binding =
         &pCreateInfo->pBindings[i];

      binding_get_desc_count(binding, &desc_count, &dyn_ubo_count,
                             &dyn_ssbo_count);
      has_sampler_desc = has_sampler_desc || is_sampler(binding);
      has_texture_desc = has_texture_desc || is_texture(binding);
   }

   if (has_texture_desc && !has_sampler_desc)
      desc_count++;

   if (desc_count > PANVK_MAX_DESCS_PER_SET ||
       dyn_ubo_count > MAX_DYNAMIC_UNIFORM_BUFFERS ||
       dyn_ssbo_count > MAX_DYNAMIC_STORAGE_BUFFERS)
      return;

   pSupport->supported = true;
}

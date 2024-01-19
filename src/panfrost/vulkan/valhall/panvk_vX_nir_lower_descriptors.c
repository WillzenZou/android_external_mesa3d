/*
 * Copyright © 2024 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "panvk_vX_descriptor_set.h"
#include "panvk_vX_descriptor_set_layout.h"
#include "panvk_vX_driver_descriptor_set.h"

#include "nir.h"
#include "nir_builder.h"

#define PANVK_VALHALL_RESOURCE_TABLE_IDX 62

struct lower_descriptors_ctx {
   const struct panvk_pipeline_layout *layout;
   const struct panfrost_compile_inputs *compile_inputs;
   bool has_img_access;
};

static const struct panvk2_descriptor_set_layout *
get_set_layout(unsigned set, const struct lower_descriptors_ctx *ctx)
{
   const struct vk_pipeline_layout *layout = &ctx->layout->vk;

   assert(set < layout->set_count);
   const struct panvk2_descriptor_set_layout *set_layout =
      vk_to_panvk2_descriptor_set_layout(layout->set_layouts[set]);

   return set_layout;
}

static const struct panvk2_descriptor_set_binding_layout *
get_binding_layout(unsigned set, unsigned binding,
                   const struct lower_descriptors_ctx *ctx)
{
   const struct panvk2_descriptor_set_layout *set_layout =
      get_set_layout(set, ctx);

   assert(binding < set_layout->binding_count);
   return &set_layout->bindings[binding];
}

static void
build_desc_index(nir_builder *b, unsigned set,
                 const struct panvk2_descriptor_set_binding_layout *layout,
                 nir_def *array_index, VkDescriptorType type,
                 unsigned *desc_index_imm, nir_def **desc_index)
{
   unsigned array_index_imm = 0;

   if (array_index != NULL) {
      nir_src src = nir_src_for_ssa(array_index);

      if (nir_src_is_const(src)) {
         array_index_imm = nir_src_as_uint(src);
         array_index = NULL;
      }
   }

   if (layout->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
       layout->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
      *desc_index_imm = panvk2_get_dyn_desc_index(layout, set, array_index_imm);
   } else {
      *desc_index_imm = panvk2_get_desc_index(layout, array_index_imm, type);
   }
   *desc_index =
      array_index != NULL
         ? nir_imul_imm(b, array_index, panvk2_get_desc_stride(layout->type))
         : NULL;
}

/**
 * Build an index as the following:
 *
 *    index = descriptor_idx | target_set << 24
 */
static nir_def *
build_index(nir_builder *b, unsigned set, unsigned binding,
            nir_def *array_index, const struct lower_descriptors_ctx *ctx)
{
   const struct panvk2_descriptor_set_binding_layout *binding_layout =
      get_binding_layout(set, binding, ctx);

   unsigned target_set;
   unsigned const_off;
   nir_def *descriptor_idx;

   build_desc_index(b, set, binding_layout, array_index, binding_layout->type,
                    &const_off, &descriptor_idx);

   if (descriptor_idx == NULL) {
      descriptor_idx = nir_imm_int(b, const_off);
   } else {
      descriptor_idx = nir_iadd_imm(b, descriptor_idx, const_off);
   }

   if (binding_layout->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
       binding_layout->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
      target_set = PANVK_DRIVER_DESC_SET;
   } else {
      target_set = set;
   }

   return nir_ior_imm(b, descriptor_idx, target_set << 24);
}

/**
 * Build a Vulkan resource index as the following:
 *
 *    vec2(index, offset)
 */
static nir_def *
build_res_index(nir_builder *b, unsigned set, unsigned binding,
                nir_def *array_index, const struct lower_descriptors_ctx *ctx)
{
   const struct panvk2_descriptor_set_binding_layout *binding_layout =
      get_binding_layout(set, binding, ctx);

   nir_def *index;

   switch (binding_layout->type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC: {
      index = build_index(b, set, binding, array_index, ctx);
      break;
   }

   default:
      unreachable("Unsupported descriptor type");
   }

   return nir_vec2(b, index, nir_imm_int(b, 0));
}

/**
 * Adjust a Vulkan resource index as the following:
 *
 *    vec2(index, offset) -> vec2(index + delta * binding_desc_stride, offset)
 */
static nir_def *
build_res_reindex(nir_builder *b, VkDescriptorType desc_type, nir_def *orig,
                  nir_def *delta)
{
   const unsigned desc_stride = panvk2_get_desc_stride(desc_type);

   nir_def *new_index =
      nir_iadd(b, nir_channel(b, orig, 0), nir_imul_imm(b, delta, desc_stride));

   return nir_vec2(b, new_index, nir_channel(b, orig, 1));
}

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex,
          const struct lower_descriptors_ctx *ctx)
{
   /* TODO */
   return false;
}

static bool
lower_res_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                 const struct lower_descriptors_ctx *ctx)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *res;
   switch (intrin->intrinsic) {
   case nir_intrinsic_vulkan_resource_index:
      res = build_res_index(b, nir_intrinsic_desc_set(intrin),
                            nir_intrinsic_binding(intrin), intrin->src[0].ssa,
                            ctx);
      break;

   case nir_intrinsic_vulkan_resource_reindex:
      res = build_res_reindex(b, nir_intrinsic_desc_type(intrin),
                              intrin->src[0].ssa, intrin->src[1].ssa);
      break;

   /* Everything following the same addr format, this is a 1:1 operation */
   case nir_intrinsic_load_vulkan_descriptor:
      res = intrin->src[0].ssa;
      break;

   default:
      unreachable("Unhandled resource intrinsic");
   }

   assert(intrin->def.bit_size == res->bit_size);
   assert(intrin->def.num_components == res->num_components);
   nir_def_rewrite_uses(&intrin->def, res);
   nir_instr_remove(&intrin->instr);

   return true;
}

static bool
lower_image_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                   const struct lower_descriptors_ctx *ctx)
{
   /* TODO */
   return false;
}

static bool
lower_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin,
                struct lower_descriptors_ctx *ctx)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_vulkan_resource_index:
   case nir_intrinsic_vulkan_resource_reindex:
   case nir_intrinsic_load_vulkan_descriptor:
      return lower_res_intrin(b, intrin, ctx);

   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic:
   case nir_intrinsic_image_deref_atomic_swap:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples:
   case nir_intrinsic_image_deref_texel_address:
      return lower_image_intrin(b, intrin, ctx);

   default:
      return false;
   }
}

static bool
lower_descriptors_instr(nir_builder *b, nir_instr *instr, void *data)
{
   struct lower_descriptors_ctx *ctx = data;

   switch (instr->type) {
   case nir_instr_type_tex:
      return lower_tex(b, nir_instr_as_tex(instr), ctx);
   case nir_instr_type_intrinsic:
      return lower_intrinsic(b, nir_instr_as_intrinsic(instr), ctx);
   default:
      return false;
   }
}

bool
panvk_per_arch(nir_lower_descriptors)(
   nir_shader *nir, const struct panvk_lower_desc_inputs *inputs,
   bool *has_img_access_out)
{
   struct lower_descriptors_ctx ctx = {
      .layout = inputs->layout,
      .compile_inputs = inputs->compile_inputs,
   };

   bool progress = nir_shader_instructions_pass(
      nir, lower_descriptors_instr,
      nir_metadata_block_index | nir_metadata_dominance, (void *)&ctx);

   if (has_img_access_out)
      *has_img_access_out = ctx.has_img_access;

   return progress;
}

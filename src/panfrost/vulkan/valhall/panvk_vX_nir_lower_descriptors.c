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

#include "panvk_descriptor_set_layout.h"
#include "panvk_vX_descriptor_set.h"
#include "panvk_vX_driver_descriptor_set.h"

#include "nir.h"
#include "nir_builder.h"

#define PANVK_VALHALL_RESOURCE_TABLE_IDX 62

struct lower_descriptors_ctx {
   const struct panvk_pipeline_layout *layout;
   const struct panfrost_compile_inputs *compile_inputs;
   bool has_img_access;
};

static const struct panvk_descriptor_set_layout *
get_set_layout(unsigned set, const struct lower_descriptors_ctx *ctx)
{
   const struct vk_pipeline_layout *layout = &ctx->layout->vk;

   assert(set < layout->set_count);
   const struct panvk_descriptor_set_layout *set_layout =
      vk_to_panvk_descriptor_set_layout(layout->set_layouts[set]);

   return set_layout;
}

static const struct panvk_descriptor_set_binding_layout *
get_binding_layout(unsigned set, unsigned binding,
                   const struct lower_descriptors_ctx *ctx)
{
   const struct panvk_descriptor_set_layout *set_layout =
      get_set_layout(set, ctx);

   assert(binding < set_layout->binding_count);
   return &set_layout->bindings[binding];
}

static void
get_resource_deref_binding(nir_builder *b, nir_deref_instr *deref,
                           unsigned *set, unsigned *binding, nir_def **index)
{
   if (deref->deref_type == nir_deref_type_array) {
      *index = deref->arr.index.ssa;
      deref = nir_deref_instr_parent(deref);
   } else {
      *index = nir_imm_int(b, 0);
   }

   assert(deref->deref_type == nir_deref_type_var);
   nir_variable *var = deref->var;

   *set = var->data.descriptor_set;
   *binding = var->data.binding;
}

static void
build_desc_index(nir_builder *b, unsigned set,
                 const struct panvk_descriptor_set_binding_layout *layout,
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
   const struct panvk_descriptor_set_binding_layout *binding_layout =
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
   const struct panvk_descriptor_set_binding_layout *binding_layout =
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

static void
tex_desc_get_index_offset(nir_builder *b, nir_deref_instr *deref,
                          const struct lower_descriptors_ctx *ctx,
                          nir_def **table_idx, nir_def **desc_offset)
{
   unsigned set, binding;
   nir_def *array_index;
   get_resource_deref_binding(b, deref, &set, &binding, &array_index);
   const struct panvk_descriptor_set_binding_layout *binding_layout =
      get_binding_layout(set, binding, ctx);

   const unsigned desc_stride = panvk2_get_desc_stride(binding_layout->type);
   const unsigned desc_index = panvk2_get_desc_index(
      binding_layout, 0, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

   *table_idx =
      nir_imm_int(b, pan_res_handle(PANVK_VALHALL_RESOURCE_TABLE_IDX, set));
   *desc_offset = nir_iadd_imm(b, nir_imul_imm(b, array_index, desc_stride),
                               desc_index * PANVK_DESCRIPTOR_SIZE);
}

static nir_def *
load_tex_img_size(nir_builder *b, nir_deref_instr *deref,
                  unsigned coord_components, bool is_array,
                  unsigned dest_components,
                  const struct lower_descriptors_ctx *ctx)
{
   nir_def *table_idx;
   nir_def *desc_offset;
   tex_desc_get_index_offset(b, deref, ctx, &table_idx, &desc_offset);

   if (is_array)
      coord_components--;

   nir_def *comps[3];

   assert(coord_components != 3 || !is_array);
   assert(dest_components <= 3);

   /* S/T dimension is encoded in MALI_TEXTURE::word[1].bits[0:31] with 1
    * subtracted. */
   nir_def *xy_size = nir_load_ubo(
      b, 1, 32, table_idx, nir_iadd_imm(b, desc_offset, 0x4), .range = ~0u,
      .align_mul = PANVK_DESCRIPTOR_SIZE, .align_offset = 0x4);

   if (coord_components == 1) {
      /* 1D images store their size in a 32 bits field. */
      comps[0] = xy_size;
   } else {
      /* All others images type store their size with 16 bits. */
      comps[0] = nir_u2u32(b, nir_unpack_32_2x16_split_x(b, xy_size));
      comps[1] = nir_u2u32(b, nir_unpack_32_2x16_split_y(b, xy_size));

      /* R dimension is encoded in MALI_TEXTURE::word[7].bits[0:15] with 1
       * subtracted. */
      if (coord_components == 3) {
         comps[2] = nir_u2u32(
            b, nir_load_ubo(b, 1, 16, table_idx,
                            nir_iadd_imm(b, desc_offset, 0x1c), .range = ~0u,
                            .align_mul = PANVK_DESCRIPTOR_SIZE,
                            .align_offset = 0x1c));
      }
   }

   /* Array size is encoded in MALI_TEXTURE::word[6].bits[0:15] with 1
    * subtracted. */
   if (is_array) {
      comps[coord_components] = nir_u2u32(
         b,
         nir_load_ubo(b, 1, 16, table_idx, nir_iadd_imm(b, desc_offset, 0x18),
                      .range = ~0u, .align_mul = PANVK_DESCRIPTOR_SIZE,
                      .align_offset = 0x18));
   }

   /* All sizes are encoded with 1 subtracted. */
   return nir_iadd_imm(b, nir_vec(b, comps, dest_components), 1);
}

static nir_def *
load_tex_img_levels(nir_builder *b, nir_deref_instr *deref,
                    const struct lower_descriptors_ctx *ctx)
{
   nir_def *table_idx;
   nir_def *desc_offset;
   tex_desc_get_index_offset(b, deref, ctx, &table_idx, &desc_offset);

   /* Number of levels is encoded in MALI_TEXTURE::word[2].bits[16:20] with 1
    * subtracted. */
   nir_def *raw_value = nir_load_ubo(
      b, 1, 32, table_idx, nir_iadd_imm(b, desc_offset, 0x8), .range = ~0u,
      .align_mul = PANVK_DESCRIPTOR_SIZE, .align_offset = 0x8);
   nir_def *mip_levels_minus_one =
      nir_iand_imm(b, nir_ishr_imm(b, raw_value, 16), 0xf);

   return nir_iadd_imm(b, mip_levels_minus_one, 1);
}

static nir_def *
load_tex_img_samples(nir_builder *b, nir_deref_instr *deref,
                     const struct lower_descriptors_ctx *ctx)
{
   nir_def *table_idx;
   nir_def *desc_offset;
   tex_desc_get_index_offset(b, deref, ctx, &table_idx, &desc_offset);

   /* Multisample count is encoded in MALI_TEXTURE::word[3].bits[13:15] as the
    * exponent of a power of 2. */
   nir_def *raw_value = nir_load_ubo(
      b, 1, 32, table_idx, nir_iadd_imm(b, desc_offset, 0xc), .range = ~0u,
      .align_mul = PANVK_DESCRIPTOR_SIZE, .align_offset = 0xc);
   nir_def *ms_count_type = nir_iand_imm(b, nir_ishr_imm(b, raw_value, 13), 7);

   return nir_ishl(b, nir_imm_int(b, 1), ms_count_type);
}

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex,
          const struct lower_descriptors_ctx *ctx)
{
   b->cursor = nir_before_instr(&tex->instr);

   const int texture_src_idx =
      nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   const int sampler_src_idx =
      nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);

   if (texture_src_idx < 0) {
      assert(sampler_src_idx < 0);
      return false;
   }

   nir_deref_instr *texture = nir_src_as_deref(tex->src[texture_src_idx].src);
   assert(texture);

   if (tex->op == nir_texop_txs || tex->op == nir_texop_query_levels ||
       tex->op == nir_texop_texture_samples) {
      unsigned coord_components =
         glsl_get_sampler_dim_coordinate_components(tex->sampler_dim);
      const bool is_array = tex->is_array;

      if (tex->sampler_dim != GLSL_SAMPLER_DIM_CUBE)
         coord_components += is_array;

      nir_def *res;
      switch (tex->op) {
      case nir_texop_txs:
         res = load_tex_img_size(b, texture, coord_components, is_array,
                                 tex->def.num_components, ctx);
         break;
      case nir_texop_query_levels:
         res = load_tex_img_levels(b, texture, ctx);
         break;
      case nir_texop_texture_samples:
         res = load_tex_img_samples(b, texture, ctx);
         break;
      default:
         unreachable("Unsupported texture query op");
      }

      nir_def_rewrite_uses(&tex->def, res);
      nir_instr_remove(&tex->instr);
      return true;
   }

   nir_deref_instr *sampler =
      sampler_src_idx < 0 ? NULL
                          : nir_src_as_deref(tex->src[sampler_src_idx].src);

   unsigned tex_set, tex_binding;
   nir_def *tex_array_index;
   get_resource_deref_binding(b, texture, &tex_set, &tex_binding,
                              &tex_array_index);

   unsigned sampler_set, sampler_binding;
   nir_def *sampler_array_index;

   /*
    * Valhall ISA enforce a sampler for every texture ops.
    * panvk2 should have created an entry as the first binding in the set.
    */
   if (sampler == NULL) {
      sampler_set = tex_set;
      sampler_binding = 0;
      sampler_array_index = NULL;
   } else {
      get_resource_deref_binding(b, sampler, &sampler_set, &sampler_binding,
                                 &sampler_array_index);
   }

   const struct panvk_descriptor_set_binding_layout *sampler_binding_layout =
      get_binding_layout(sampler_set, sampler_binding, ctx);
   unsigned sampler_desc_index_imm;
   nir_def *sampler_desc_index;
   build_desc_index(b, sampler_set, sampler_binding_layout, sampler_array_index,
                    VK_DESCRIPTOR_TYPE_SAMPLER, &sampler_desc_index_imm,
                    &sampler_desc_index);

   tex->sampler_index = pan_res_handle(sampler_set, sampler_desc_index_imm);

   if (sampler_desc_index != NULL) {
      nir_tex_instr_add_src(tex, nir_tex_src_sampler_offset,
                            sampler_desc_index);
   }

   const struct panvk_descriptor_set_binding_layout *tex_binding_layout =
      get_binding_layout(tex_set, tex_binding, ctx);
   unsigned tex_desc_index_imm;
   nir_def *tex_desc_index;
   build_desc_index(b, tex_set, tex_binding_layout, tex_array_index,
                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, &tex_desc_index_imm,
                    &tex_desc_index);

   tex->texture_index = pan_res_handle(tex_set, tex_desc_index_imm);

   if (tex_desc_index != NULL) {
      nir_tex_instr_add_src(tex, nir_tex_src_texture_offset, tex_desc_index);
   }

   return true;
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
                   struct lower_descriptors_ctx *ctx)
{
   b->cursor = nir_before_instr(&intrin->instr);
   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

   if (intrin->intrinsic == nir_intrinsic_image_deref_size ||
       intrin->intrinsic == nir_intrinsic_image_deref_samples) {
      const unsigned coord_components =
         nir_image_intrinsic_coord_components(intrin);
      const bool is_array = nir_intrinsic_image_array(intrin);

      nir_def *res;
      switch (intrin->intrinsic) {
      case nir_intrinsic_image_deref_size:
         res = load_tex_img_size(b, deref, coord_components, is_array,
                                 intrin->def.num_components, ctx);
         break;
      case nir_intrinsic_image_deref_samples:
         res = load_tex_img_samples(b, deref, ctx);
         break;
      default:
         unreachable("Unsupported image query op");
      }

      nir_def_rewrite_uses(&intrin->def, res);
      nir_instr_remove(&intrin->instr);
   } else {
      unsigned set, binding;
      nir_def *array_index;
      get_resource_deref_binding(b, deref, &set, &binding, &array_index);

      nir_rewrite_image_intrinsic(
         intrin, build_index(b, set, binding, array_index, ctx), false);
      ctx->has_img_access = true;
   }

   return true;
}

static bool
lower_input_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                   const struct lower_descriptors_ctx *ctx)
{
   /* We always use heap-based varying allocation when IDVS is used on Valhall. */
   bool malloc_idvs = !ctx->compile_inputs->no_idvs;

   /* All vertex attributes come from the driver descriptor set starting at
    * vertex_attribs. Fragment inputs come from it too, unless they've been
    * allocated on the heap.
    */
   if (b->shader->info.stage == MESA_SHADER_VERTEX ||
       (b->shader->info.stage == MESA_SHADER_FRAGMENT && !malloc_idvs)) {
      const unsigned attribute_base_index =
         panvk2_driver_descriptor_set_idx(vertex_attribs[0]);

      nir_intrinsic_set_base(
         intrin,
         pan_res_handle(PANVK_DRIVER_DESC_SET,
                        attribute_base_index + nir_intrinsic_base(intrin)));
      return true;
   }

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

   case nir_intrinsic_load_input:
      return lower_input_intrin(b, intrin, ctx);

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

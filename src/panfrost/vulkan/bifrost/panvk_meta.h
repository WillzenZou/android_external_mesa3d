/*
 * Copyright (C) 2023 Collabora Ltd.
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

#ifndef PANVK_PRIVATE_H
#error "Must be included from panvk_private.h"
#endif

#include "../panvk_mempool.h"

#define PANVK_META_COPY_BUF2IMG_NUM_FORMATS  12
#define PANVK_META_COPY_IMG2BUF_NUM_FORMATS  12
#define PANVK_META_COPY_IMG2IMG_NUM_FORMATS  14
#define PANVK_META_COPY_NUM_TEX_TYPES        5
#define PANVK_META_COPY_BUF2BUF_NUM_BLKSIZES 5

struct panvk_meta {

   struct panvk_pool bin_pool;
   struct panvk_pool desc_pool;

   /* Access to the blitter pools are protected by the blitter
    * shader/rsd locks. They can't be merged with other binary/desc
    * pools unless we patch pan_blitter.c to external pool locks.
    */
   struct {
      struct panvk_pool bin_pool;
      struct panvk_pool desc_pool;
      struct pan_blitter_cache cache;
   } blitter;

   struct pan_blend_shader_cache blend_shader_cache;

   struct {
      struct {
         mali_ptr shader;
         struct pan_shader_info shader_info;
      } color[3]; /* 3 base types */
   } clear_attachment;

   struct {
      struct {
         mali_ptr rsd;
      } buf2img[PANVK_META_COPY_BUF2IMG_NUM_FORMATS];
      struct {
         mali_ptr rsd;
      } img2buf[PANVK_META_COPY_NUM_TEX_TYPES]
               [PANVK_META_COPY_IMG2BUF_NUM_FORMATS];
      struct {
         mali_ptr rsd;
      } img2img[2][PANVK_META_COPY_NUM_TEX_TYPES]
               [PANVK_META_COPY_IMG2IMG_NUM_FORMATS];
      struct {
         mali_ptr rsd;
      } buf2buf[PANVK_META_COPY_BUF2BUF_NUM_BLKSIZES];
      struct {
         mali_ptr rsd;
      } fillbuf;
   } copy;
};

static inline unsigned
panvk_meta_copy_tex_type(unsigned dim, bool isarray)
{
   assert(dim > 0 && dim <= 3);
   assert(dim < 3 || !isarray);
   return (((dim - 1) << 1) | (isarray ? 1 : 0));
}

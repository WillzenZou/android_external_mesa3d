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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __PAN_CSF_H__
#define __PAN_CSF_H__

#include "pan_bo.h"

struct ceu_builder;

struct panfrost_csf_batch {
   /* CS related fields. */
   struct {
      /* CS builder. */
      struct ceu_builder *builder;

      /* CS state, written through the CS, and checked when PAN_MESA_DEBUG=sync. */
      struct panfrost_ptr state;
   } cs;
};

struct panfrost_csf_context {
   uint32_t group_handle;

   struct {
      uint32_t handle;
      struct panfrost_bo *desc_bo;
   } heap;

   /* Temporary geometry buffer. Used as a FIFO by the tiler. */
   struct panfrost_bo *tmp_geom_bo;
};

#endif /* __PAN_CSF_H__ */

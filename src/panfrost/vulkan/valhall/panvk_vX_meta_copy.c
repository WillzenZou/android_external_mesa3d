/*
 * Copyright © 2023 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "nir/nir_builder.h"
#include "pan_encoder.h"
#include "pan_props.h"
#include "pan_shader.h"

#include "panvk_buffer.h"
#include "panvk_cmd_buffer.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_image.h"
#include "panvk_physical_device.h"

void
panvk_per_arch(CmdCopyImage2)(VkCommandBuffer commandBuffer,
                              const VkCopyImageInfo2 *pCopyImageInfo)
{
   panvk_stub();
}

void
panvk_per_arch(CmdCopyBufferToImage2)(
   VkCommandBuffer commandBuffer,
   const VkCopyBufferToImageInfo2 *pCopyBufferToImageInfo)
{
   panvk_stub();
}

void
panvk_per_arch(CmdCopyImageToBuffer2)(
   VkCommandBuffer commandBuffer,
   const VkCopyImageToBufferInfo2 *pCopyImageToBufferInfo)
{
   panvk_stub();
}

void
panvk_per_arch(CmdCopyBuffer2)(VkCommandBuffer commandBuffer,
                               const VkCopyBufferInfo2 *pCopyBufferInfo)
{
   panvk_stub();
}

void
panvk_per_arch(CmdFillBuffer)(VkCommandBuffer commandBuffer, VkBuffer dstBuffer,
                              VkDeviceSize dstOffset, VkDeviceSize fillSize,
                              uint32_t data)
{
   panvk_stub();
}

void
panvk_per_arch(CmdUpdateBuffer)(VkCommandBuffer commandBuffer,
                                VkBuffer dstBuffer, VkDeviceSize dstOffset,
                                VkDeviceSize dataSize, const void *pData)
{
   panvk_stub();
}

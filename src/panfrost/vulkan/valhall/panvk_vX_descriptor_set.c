/*
 * Copyright © 2023 Collabora Ltd.
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

#include "genxml/gen_macros.h"

#include "panvk_private.h"

VkResult
panvk_per_arch(CreateDescriptorSetLayout)(
   VkDevice _device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator, VkDescriptorSetLayout *pSetLayout)
{
   panvk_stub();
   return VK_SUCCESS;
}

void
panvk_per_arch(GetDescriptorSetLayoutSupport)(
   VkDevice _device, const VkDescriptorSetLayoutCreateInfo *pCreateInfo,
   VkDescriptorSetLayoutSupport *pSupport)
{
   panvk_stub();
}

VkResult
panvk_per_arch(CreatePipelineLayout)(
   VkDevice _device, const VkPipelineLayoutCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator, VkPipelineLayout *pPipelineLayout)
{
   panvk_stub();
   return VK_SUCCESS;
}

VkResult
panvk_per_arch(CreateDescriptorPool)(
   VkDevice _device, const VkDescriptorPoolCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator, VkDescriptorPool *pDescriptorPool)
{
   panvk_stub();
   return VK_SUCCESS;
}

void
panvk_per_arch(DestroyDescriptorPool)(VkDevice _device,
                                      VkDescriptorPool _pool,
                                      const VkAllocationCallbacks *pAllocator)
{
   panvk_stub();
}

VkResult
panvk_per_arch(AllocateDescriptorSets)(
   VkDevice _device, const VkDescriptorSetAllocateInfo *pAllocateInfo,
   VkDescriptorSet *pDescriptorSets)
{
   panvk_stub();
   return VK_SUCCESS;
}

VkResult
panvk_per_arch(FreeDescriptorSets)(VkDevice _device,
                                   VkDescriptorPool descriptorPool,
                                   uint32_t count,
                                   const VkDescriptorSet *pDescriptorSets)
{
   panvk_stub();
   return VK_SUCCESS;
}

VkResult
panvk_per_arch(ResetDescriptorPool)(VkDevice _device, VkDescriptorPool _pool,
                                    VkDescriptorPoolResetFlags flags)
{
   panvk_stub();
   return VK_SUCCESS;
}

void
panvk_per_arch(UpdateDescriptorSets)(
   VkDevice _device, uint32_t descriptorWriteCount,
   const VkWriteDescriptorSet *pDescriptorWrites, uint32_t descriptorCopyCount,
   const VkCopyDescriptorSet *pDescriptorCopies)
{
   panvk_stub();
}

void
panvk_per_arch(UpdateDescriptorSetWithTemplate)(
   VkDevice _device, VkDescriptorSet descriptorSet,
   VkDescriptorUpdateTemplate descriptorUpdateTemplate, const void *data)
{
   panvk_stub();
}

void
panvk_per_arch(CmdBindDescriptorSets)(VkCommandBuffer commandBuffer,
                                      VkPipelineBindPoint pipelineBindPoint,
                                      VkPipelineLayout layout, uint32_t firstSet,
                                      uint32_t descriptorSetCount,
                                      const VkDescriptorSet *pDescriptorSets,
                                      uint32_t dynamicOffsetCount,
                                      const uint32_t *pDynamicOffsets)
{
   panvk_stub();
}

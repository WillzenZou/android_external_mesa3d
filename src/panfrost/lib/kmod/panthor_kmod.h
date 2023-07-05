/*
 * Copyright © 2023 Collabora, Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

struct pan_kmod_bo;
struct pan_kmod_dev;
struct pan_kmod_vm;

void panthor_kmod_bo_attach_sync_point(struct pan_kmod_bo *bo,
                                       uint32_t sync_handle,
                                       uint64_t sync_point, bool read_only);
void panthor_kmod_bo_get_sync_point(struct pan_kmod_bo *bo,
                                    uint32_t *sync_handle, uint64_t *sync_point,
                                    bool read_only);
uint32_t panthor_kmod_vm_handle(struct pan_kmod_vm *vm);
void panthor_kmod_vm_new_sync_point(struct pan_kmod_vm *vm,
                                    uint32_t *sync_handle,
                                    uint64_t *sync_point);
uint32_t panthor_kmod_get_flush_id(const struct pan_kmod_dev *dev);

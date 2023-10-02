#include "panvk_private.h"

struct panvk_bo*
panvk_bo_alloc(struct panvk_device *dev, size_t size, uint32_t flags, const char *label)
{
   struct pan_kmod_vm *exclusive_vm = dev->physical_device->pdev.kmod.vm;

   struct panvk_bo *bo = calloc(1, sizeof *bo);

   bo->kmod_bo = pan_kmod_bo_alloc(dev->physical_device->pdev.kmod.dev, exclusive_vm, size, flags);
   bo->device_ptr = pan_kmod_vm_map(dev->physical_device->pdev.kmod.vm, bo->kmod_bo, PAN_KMOD_VM_MAP_AUTO_VA, 0, bo->kmod_bo->size);

   return bo;
}

void
panvk_bo_free(struct panvk_device *dev, struct panvk_bo *bo)
{
   panvk_bo_munmap(dev, bo);
   pan_kmod_vm_unmap(dev->physical_device->pdev.kmod.vm, bo->device_ptr, bo->kmod_bo->size);
   pan_kmod_bo_free(bo->kmod_bo);
   free(bo);
}

void
panvk_bo_mmap(struct panvk_device *dev, struct panvk_bo *bo)
{
   if (bo->host_ptr)
      return;

   bo->host_ptr = pan_kmod_bo_mmap(bo->kmod_bo, 0, bo->kmod_bo->size, PROT_READ | PROT_WRITE, MAP_SHARED);
   if (bo->host_ptr == MAP_FAILED) {
      bo->host_ptr = NULL;
      fprintf(stderr, "mmap failed: result=%p size=0x%llx\n", bo->host_ptr,
              (long long)bo->kmod_bo->size);
   }
}

void
panvk_bo_munmap(struct panvk_device *dev, struct panvk_bo *bo)
{
   if (bo->host_ptr) {
      if (os_munmap((void *)(uintptr_t)bo->host_ptr, bo->kmod_bo->size)) {
         perror("munmap");
         abort();
      }
      bo->host_ptr = NULL;
   }
}

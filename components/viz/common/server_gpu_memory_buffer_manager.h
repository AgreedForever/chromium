// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef COMPONENTS_VIZ_COMMON_SERVER_GPU_MEMORY_BUFFER_MANAGER_H_
#define COMPONENTS_VIZ_COMMON_SERVER_GPU_MEMORY_BUFFER_MANAGER_H_

#include <memory>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/threading/thread_checker.h"
#include "gpu/command_buffer/client/gpu_memory_buffer_manager.h"
#include "gpu/ipc/host/gpu_memory_buffer_support.h"

namespace ui {
namespace mojom {
class GpuService;
}
}

namespace viz {

// This GpuMemoryBufferManager implementation is for [de]allocating gpu memory
// from the gpu process over the mojom.GpuService api.
class ServerGpuMemoryBufferManager : public gpu::GpuMemoryBufferManager,
                                     public base::ThreadChecker {
 public:
  ServerGpuMemoryBufferManager(ui::mojom::GpuService* gpu_service,
                               int client_id);
  ~ServerGpuMemoryBufferManager() override;

  void DestroyGpuMemoryBuffer(gfx::GpuMemoryBufferId id,
                              int client_id,
                              const gpu::SyncToken& sync_token);

  void DestroyAllGpuMemoryBufferForClient(int client_id);

  gfx::GpuMemoryBufferHandle CreateGpuMemoryBufferHandle(
      gfx::GpuMemoryBufferId id,
      int client_id,
      const gfx::Size& size,
      gfx::BufferFormat format,
      gfx::BufferUsage usage,
      gpu::SurfaceHandle surface_handle);

  // Overridden from gpu::GpuMemoryBufferManager:
  std::unique_ptr<gfx::GpuMemoryBuffer> CreateGpuMemoryBuffer(
      const gfx::Size& size,
      gfx::BufferFormat format,
      gfx::BufferUsage usage,
      gpu::SurfaceHandle surface_handle) override;
  void SetDestructionSyncToken(gfx::GpuMemoryBuffer* buffer,
                               const gpu::SyncToken& sync_token) override;

 private:
  ui::mojom::GpuService* gpu_service_;
  const int client_id_;
  int next_gpu_memory_id_ = 1;

  using NativeBuffers =
      std::unordered_set<gfx::GpuMemoryBufferId,
                         BASE_HASH_NAMESPACE::hash<gfx::GpuMemoryBufferId>>;
  std::unordered_map<int, NativeBuffers> native_buffers_;

  const gpu::GpuMemoryBufferConfigurationSet native_configurations_;
  base::WeakPtrFactory<ServerGpuMemoryBufferManager> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(ServerGpuMemoryBufferManager);
};

}  // namespace viz

#endif  // COMPONENTS_VIZ_COMMON_SERVER_GPU_MEMORY_BUFFER_MANAGER_H_

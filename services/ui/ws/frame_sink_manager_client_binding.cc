// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/ui/ws/frame_sink_manager_client_binding.h"

#include "services/ui/ws/gpu_host.h"

namespace ui {
namespace ws {

FrameSinkManagerClientBinding::FrameSinkManagerClientBinding(
    cc::mojom::FrameSinkManagerClient* frame_sink_manager_client,
    GpuHost* gpu_host)
    : frame_sink_manager_client_binding_(frame_sink_manager_client) {
  cc::mojom::FrameSinkManagerClientPtr client_proxy;
  frame_sink_manager_client_binding_.Bind(mojo::MakeRequest(&client_proxy));
  gpu_host->CreateFrameSinkManager(mojo::MakeRequest(&frame_sink_manager_),
                                   std::move(client_proxy));
}

FrameSinkManagerClientBinding::~FrameSinkManagerClientBinding() = default;

void FrameSinkManagerClientBinding::CreateRootCompositorFrameSink(
    const cc::FrameSinkId& frame_sink_id,
    gpu::SurfaceHandle surface_handle,
    cc::mojom::MojoCompositorFrameSinkAssociatedRequest request,
    cc::mojom::MojoCompositorFrameSinkPrivateRequest private_request,
    cc::mojom::MojoCompositorFrameSinkClientPtr client,
    cc::mojom::DisplayPrivateAssociatedRequest display_private_request) {
  frame_sink_manager_->CreateRootCompositorFrameSink(
      frame_sink_id, surface_handle, std::move(request),
      std::move(private_request), std::move(client),
      std::move(display_private_request));
}

void FrameSinkManagerClientBinding::CreateCompositorFrameSink(
    const cc::FrameSinkId& frame_sink_id,
    cc::mojom::MojoCompositorFrameSinkRequest request,
    cc::mojom::MojoCompositorFrameSinkPrivateRequest private_request,
    cc::mojom::MojoCompositorFrameSinkClientPtr client) {
  frame_sink_manager_->CreateCompositorFrameSink(
      frame_sink_id, std::move(request), std::move(private_request),
      std::move(client));
}

void FrameSinkManagerClientBinding::RegisterFrameSinkHierarchy(
    const cc::FrameSinkId& parent_frame_sink_id,
    const cc::FrameSinkId& child_frame_sink_id) {
  frame_sink_manager_->RegisterFrameSinkHierarchy(parent_frame_sink_id,
                                                  child_frame_sink_id);
}

void FrameSinkManagerClientBinding::UnregisterFrameSinkHierarchy(
    const cc::FrameSinkId& parent_frame_sink_id,
    const cc::FrameSinkId& child_frame_sink_id) {
  frame_sink_manager_->UnregisterFrameSinkHierarchy(parent_frame_sink_id,
                                                    child_frame_sink_id);
}

void FrameSinkManagerClientBinding::DropTemporaryReference(
    const cc::SurfaceId& surface_id) {
  frame_sink_manager_->DropTemporaryReference(surface_id);
}

}  // namespace ws
}  // namespace ui

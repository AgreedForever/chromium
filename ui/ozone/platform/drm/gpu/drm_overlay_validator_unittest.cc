// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ui/ozone/platform/drm/gpu/drm_overlay_validator.h"

#include <drm_fourcc.h>

#include <memory>
#include <utility>

#include "base/message_loop/message_loop.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/ozone/common/gpu/ozone_gpu_message_params.h"
#include "ui/ozone/platform/drm/common/drm_util.h"
#include "ui/ozone/platform/drm/gpu/crtc_controller.h"
#include "ui/ozone/platform/drm/gpu/drm_device_generator.h"
#include "ui/ozone/platform/drm/gpu/drm_device_manager.h"
#include "ui/ozone/platform/drm/gpu/drm_window.h"
#include "ui/ozone/platform/drm/gpu/fake_plane_info.h"
#include "ui/ozone/platform/drm/gpu/hardware_display_controller.h"
#include "ui/ozone/platform/drm/gpu/mock_drm_device.h"
#include "ui/ozone/platform/drm/gpu/mock_hardware_display_plane_manager.h"
#include "ui/ozone/platform/drm/gpu/mock_scanout_buffer.h"
#include "ui/ozone/platform/drm/gpu/mock_scanout_buffer_generator.h"
#include "ui/ozone/platform/drm/gpu/screen_manager.h"

namespace {

// Mode of size 6x4.
const drmModeModeInfo kDefaultMode =
    {0, 6, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, {'\0'}};

const gfx::AcceleratedWidget kDefaultWidgetHandle = 1;
const uint32_t kDefaultCrtc = 1;
const uint32_t kDefaultConnector = 2;
const uint32_t kSecondaryCrtc = 3;
const uint32_t kSecondaryConnector = 4;
const size_t kPlanesPerCrtc = 1;

}  // namespace

class DrmOverlayValidatorTest : public testing::Test {
 public:
  DrmOverlayValidatorTest() {}

  void SetUp() override;
  void TearDown() override;

  void OnSwapBuffers(gfx::SwapResult result) {
    on_swap_buffers_count_++;
    last_swap_buffers_result_ = result;
  }

  scoped_refptr<ui::ScanoutBuffer> ProcessBuffer(const gfx::Size& size,
                                                 uint32_t format) {
    return buffer_generator_->Create(drm_, format, size);
  }

  scoped_refptr<ui::ScanoutBuffer> ReturnNullBuffer(const gfx::Size& size,
                                                    uint32_t format) {
    return nullptr;
  }

 protected:
  std::unique_ptr<base::MessageLoop> message_loop_;
  scoped_refptr<ui::MockDrmDevice> drm_;
  std::unique_ptr<ui::MockScanoutBufferGenerator> buffer_generator_;
  std::unique_ptr<ui::ScreenManager> screen_manager_;
  std::unique_ptr<ui::DrmDeviceManager> drm_device_manager_;
  ui::MockHardwareDisplayPlaneManager* plane_manager_;
  ui::DrmWindow* window_;
  std::unique_ptr<ui::DrmOverlayValidator> overlay_validator_;
  std::vector<ui::OverlayCheck_Params> overlay_params_;
  ui::OverlayPlaneList plane_list_;
  ui::OverlayPlane::ProcessBufferCallback process_buffer_handler_;

  int on_swap_buffers_count_;
  gfx::SwapResult last_swap_buffers_result_;
  gfx::Rect overlay_rect_;
  gfx::Rect primary_rect_;

 private:
  DISALLOW_COPY_AND_ASSIGN(DrmOverlayValidatorTest);
};

void DrmOverlayValidatorTest::SetUp() {
  on_swap_buffers_count_ = 0;
  last_swap_buffers_result_ = gfx::SwapResult::SWAP_FAILED;
  process_buffer_handler_ = base::Bind(&DrmOverlayValidatorTest::ProcessBuffer,
                                       base::Unretained(this));

  message_loop_.reset(new base::MessageLoopForUI);
  std::vector<uint32_t> crtcs;
  crtcs.push_back(kDefaultCrtc);
  drm_ = new ui::MockDrmDevice(false, crtcs, kPlanesPerCrtc);
  buffer_generator_.reset(new ui::MockScanoutBufferGenerator());
  screen_manager_.reset(new ui::ScreenManager(buffer_generator_.get()));
  screen_manager_->AddDisplayController(drm_, kDefaultCrtc, kDefaultConnector);
  screen_manager_->ConfigureDisplayController(
      drm_, kDefaultCrtc, kDefaultConnector, gfx::Point(), kDefaultMode);

  drm_device_manager_.reset(new ui::DrmDeviceManager(nullptr));

  std::unique_ptr<ui::DrmWindow> window(new ui::DrmWindow(
      kDefaultWidgetHandle, drm_device_manager_.get(), screen_manager_.get()));
  window->Initialize(buffer_generator_.get());
  window->SetBounds(
      gfx::Rect(gfx::Size(kDefaultMode.hdisplay, kDefaultMode.vdisplay)));
  screen_manager_->AddWindow(kDefaultWidgetHandle, std::move(window));
  plane_manager_ =
      static_cast<ui::MockHardwareDisplayPlaneManager*>(drm_->plane_manager());
  window_ = screen_manager_->GetWindow(kDefaultWidgetHandle);
  overlay_validator_.reset(
      new ui::DrmOverlayValidator(window_, buffer_generator_.get()));

  overlay_rect_ =
      gfx::Rect(0, 0, kDefaultMode.hdisplay / 2, kDefaultMode.vdisplay / 2);

  primary_rect_ = gfx::Rect(0, 0, kDefaultMode.hdisplay, kDefaultMode.vdisplay);

  ui::OverlayCheck_Params primary_candidate;
  primary_candidate.buffer_size = primary_rect_.size();
  primary_candidate.display_rect = primary_rect_;
  primary_candidate.format = gfx::BufferFormat::BGRX_8888;
  overlay_params_.push_back(primary_candidate);

  ui::OverlayCheck_Params overlay_candidate;
  overlay_candidate.buffer_size = overlay_rect_.size();
  overlay_candidate.display_rect = overlay_rect_;
  overlay_candidate.plane_z_order = 1;
  overlay_candidate.format = gfx::BufferFormat::BGRX_8888;
  overlay_params_.push_back(overlay_candidate);

  scoped_refptr<ui::DrmDevice> drm =
      window_->GetController()->GetAllocationDrmDevice();
  for (const auto& param : overlay_params_) {
    scoped_refptr<ui::ScanoutBuffer> scanout_buffer = buffer_generator_->Create(
        drm, ui::GetFourCCFormatFromBufferFormat(param.format),
        param.buffer_size);
    ui::OverlayPlane plane(std::move(scanout_buffer), param.plane_z_order,
                           param.transform, param.display_rect, param.crop_rect,
                           process_buffer_handler_);
    plane_list_.push_back(plane);
  }
}

void DrmOverlayValidatorTest::TearDown() {
  std::unique_ptr<ui::DrmWindow> window =
      screen_manager_->RemoveWindow(kDefaultWidgetHandle);
  window->Shutdown();
  message_loop_.reset();
}

TEST_F(DrmOverlayValidatorTest, WindowWithNoController) {
  // We should never promote layers to overlay when controller is not
  // present.
  ui::HardwareDisplayController* controller = window_->GetController();
  window_->SetController(nullptr);
  std::vector<ui::OverlayCheckReturn_Params> returns =
      overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());
  EXPECT_EQ(returns.front().status, ui::OverlayCheckReturn_Params::Status::NOT);
  EXPECT_EQ(returns.back().status, ui::OverlayCheckReturn_Params::Status::NOT);
  window_->SetController(controller);
}

TEST_F(DrmOverlayValidatorTest, DontPromoteMoreLayersThanAvailablePlanes) {
  std::vector<ui::OverlayCheckReturn_Params> returns =
      overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());
  EXPECT_EQ(returns.front().status,
            ui::OverlayCheckReturn_Params::Status::ABLE);
  EXPECT_EQ(returns.back().status, ui::OverlayCheckReturn_Params::Status::NOT);
}

TEST_F(DrmOverlayValidatorTest, DontCollapseOverlayToPrimaryInFullScreen) {
  // Overlay Validator should not collapse planes during validation.
  overlay_params_.back().buffer_size = primary_rect_.size();
  overlay_params_.back().display_rect = primary_rect_;
  plane_list_.back().display_bounds = primary_rect_;

  std::vector<ui::OverlayCheckReturn_Params> returns =
      overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());
  // Second candidate should be marked as Invalid as we have only one plane
  // per CRTC.
  EXPECT_EQ(returns.front().status,
            ui::OverlayCheckReturn_Params::Status::ABLE);
  EXPECT_EQ(returns.back().status, ui::OverlayCheckReturn_Params::Status::NOT);
}

TEST_F(DrmOverlayValidatorTest, ClearCacheOnReset) {
  // This test checks if we invalidate cache when Reset is called.
  overlay_params_.back().buffer_size = overlay_rect_.size();
  overlay_params_.back().display_rect = overlay_rect_;
  plane_list_.back().display_bounds = overlay_rect_;
  std::vector<uint32_t> xrgb_yuv_packed_formats = {DRM_FORMAT_XRGB8888,
                                                   DRM_FORMAT_UYVY};

  ui::FakePlaneInfo primary_plane_info(
      100, 1 << 0, std::vector<uint32_t>(1, DRM_FORMAT_XRGB8888));
  ui::FakePlaneInfo overlay_info(101, 1 << 0, xrgb_yuv_packed_formats);
  std::vector<ui::FakePlaneInfo> planes_info{primary_plane_info, overlay_info};
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());

  ui::OverlayPlaneList plane_list =
      overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  EXPECT_EQ(DRM_FORMAT_XRGB8888,
            plane_list.back().buffer->GetFramebufferPixelFormat());
  // Check if ClearCache actually clears the cache.
  overlay_validator_->ClearCache();
  plane_list = overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  // There should be no entry in cache for this configuration and should return
  // default value of DRM_FORMAT_XRGB8888.
  EXPECT_EQ(DRM_FORMAT_XRGB8888,
            plane_list.back().buffer->GetFramebufferPixelFormat());
}

TEST_F(DrmOverlayValidatorTest, ClearCacheOnResetWithScaling) {
  // This test checks if we invalidate cache when Reset is called.
  gfx::RectF crop_rect = gfx::RectF(0, 0, 0.5, 0.5);
  overlay_params_.back().buffer_size = overlay_rect_.size();
  overlay_params_.back().display_rect = overlay_rect_;
  overlay_params_.back().crop_rect = crop_rect;
  plane_list_.back().display_bounds = overlay_rect_;
  plane_list_.back().crop_rect = crop_rect;
  std::vector<uint32_t> xrgb_yuv_packed_formats = {DRM_FORMAT_XRGB8888,
                                                   DRM_FORMAT_UYVY};

  ui::FakePlaneInfo primary_plane_info(
      100, 1 << 0, std::vector<uint32_t>(1, DRM_FORMAT_XRGB8888));
  ui::FakePlaneInfo overlay_info(101, 1 << 0, xrgb_yuv_packed_formats);
  std::vector<ui::FakePlaneInfo> planes_info{primary_plane_info, overlay_info};
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());

  ui::OverlayPlaneList plane_list =
      overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  // Scaling allows format conversion.
  EXPECT_EQ(DRM_FORMAT_UYVY,
            plane_list.back().buffer->GetFramebufferPixelFormat());
  // Check if ClearCache actually clears the cache.
  overlay_validator_->ClearCache();
  plane_list = overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  // There should be no entry in cache for this configuration and should return
  // default value of DRM_FORMAT_XRGB8888.
  EXPECT_EQ(DRM_FORMAT_XRGB8888,
            plane_list.back().buffer->GetFramebufferPixelFormat());
}

TEST_F(DrmOverlayValidatorTest, OptimalFormatForOverlayInFullScreen_XRGB) {
  // Optimal format for Overlay configuration should be XRGB, when primary plane
  // supports only XRGB and overlay obscures primary.
  overlay_params_.back().buffer_size = primary_rect_.size();
  overlay_params_.back().display_rect = primary_rect_;
  plane_list_.back().display_bounds = primary_rect_;

  // Check optimal format for Overlay.
  ui::FakePlaneInfo primary_plane_info(
      100, 1 << 0, std::vector<uint32_t>(1, DRM_FORMAT_XRGB8888));
  std::vector<uint32_t> xrgb_yuv_packed_formats = {DRM_FORMAT_XRGB8888,
                                                   DRM_FORMAT_UYVY};
  ui::FakePlaneInfo plane_info(101, 1 << 0, xrgb_yuv_packed_formats);

  std::vector<ui::FakePlaneInfo> planes{primary_plane_info, plane_info};
  plane_manager_->SetPlaneProperties(planes);
  overlay_validator_->ClearCache();

  overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());
  ui::OverlayPlaneList plane_list =
      overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  EXPECT_EQ(DRM_FORMAT_XRGB8888,
            plane_list.back().buffer->GetFramebufferPixelFormat());
}

TEST_F(DrmOverlayValidatorTest, OptimalFormatForOverlayInFullScreen_YUV) {
  overlay_params_.back().buffer_size = primary_rect_.size();
  overlay_params_.back().display_rect = primary_rect_;
  plane_list_.back().display_bounds = primary_rect_;
  std::vector<uint32_t> xrgb_yuv_packed_formats = {DRM_FORMAT_XRGB8888,
                                                   DRM_FORMAT_UYVY};

  // We should prefer YUV when primary can support it.
  ui::FakePlaneInfo primary_plane_info(100, 1 << 0, xrgb_yuv_packed_formats);
  ui::FakePlaneInfo plane_info(101, 1 << 0, xrgb_yuv_packed_formats);

  std::vector<ui::FakePlaneInfo> planes{primary_plane_info, plane_info};
  plane_manager_->SetPlaneProperties(planes);
  overlay_validator_->ClearCache();

  overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());
  ui::OverlayPlaneList plane_list =
      overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  // TODO(dcastagna): If Atomic support is enabled, a packed format (UYVY) might
  // be the optimal one and should be preferred.
  EXPECT_EQ(DRM_FORMAT_XRGB8888,
            plane_list.back().buffer->GetFramebufferPixelFormat());
}

TEST_F(DrmOverlayValidatorTest, OverlayPreferredFormat) {
  plane_manager_->ResetPlaneCount();
  // This test checks for optimal format in case of non full screen video case.
  overlay_params_.back().buffer_size = overlay_rect_.size();
  overlay_params_.back().display_rect = overlay_rect_;
  plane_list_.back().display_bounds = overlay_rect_;

  std::vector<uint32_t> xrgb_yuv_packed_formats = {DRM_FORMAT_XRGB8888,
                                                   DRM_FORMAT_UYVY};
  ui::FakePlaneInfo primary_plane_info(
      100, 1 << 0, std::vector<uint32_t>(1, DRM_FORMAT_XRGB8888));
  ui::FakePlaneInfo overlay_info(101, 1 << 0, xrgb_yuv_packed_formats);
  std::vector<ui::FakePlaneInfo> planes_info{primary_plane_info, overlay_info};
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  std::vector<ui::OverlayCheckReturn_Params> returns =
      overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());

  for (const auto& param : returns)
    EXPECT_EQ(param.status, ui::OverlayCheckReturn_Params::Status::ABLE);

  EXPECT_EQ(3, plane_manager_->plane_count());

  ui::OverlayPlaneList plane_list =
      overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  EXPECT_EQ(DRM_FORMAT_XRGB8888,
            plane_list.back().buffer->GetFramebufferPixelFormat());
}

TEST_F(DrmOverlayValidatorTest, OverlayPreferredFormat_YUV) {
  plane_manager_->ResetPlaneCount();
  // This test checks for optimal format in case of non full screen video case.
  // Prefer YUV as optimal format when Overlay supports it and scaling is
  // needed.
  gfx::RectF crop_rect = gfx::RectF(0, 0, 0.5, 0.5);
  overlay_params_.back().buffer_size = overlay_rect_.size();
  overlay_params_.back().display_rect = overlay_rect_;
  overlay_params_.back().crop_rect = crop_rect;
  plane_list_.back().display_bounds = overlay_rect_;
  plane_list_.back().crop_rect = crop_rect;

  std::vector<uint32_t> xrgb_yuv_packed_formats = {DRM_FORMAT_XRGB8888,
                                                   DRM_FORMAT_UYVY};
  ui::FakePlaneInfo primary_plane_info(
      100, 1 << 0, std::vector<uint32_t>(1, DRM_FORMAT_XRGB8888));
  ui::FakePlaneInfo overlay_info(101, 1 << 0, xrgb_yuv_packed_formats);
  std::vector<ui::FakePlaneInfo> planes_info{primary_plane_info, overlay_info};
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  std::vector<ui::OverlayCheckReturn_Params> returns =
      overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());

  for (const auto& param : returns)
    EXPECT_EQ(param.status, ui::OverlayCheckReturn_Params::Status::ABLE);

  EXPECT_EQ(5, plane_manager_->plane_count());

  ui::OverlayPlaneList plane_list =
      overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  EXPECT_EQ(DRM_FORMAT_UYVY,
            plane_list.back().buffer->GetFramebufferPixelFormat());
}

TEST_F(DrmOverlayValidatorTest, OverlayPreferredFormat_XRGB) {
  plane_manager_->ResetPlaneCount();
  // This test checks for optimal format in case of non full screen video case.
  // This should be XRGB when overlay doesn't support YUV.
  overlay_params_.back().buffer_size = overlay_rect_.size();
  overlay_params_.back().display_rect = overlay_rect_;
  plane_list_.back().display_bounds = overlay_rect_;

  std::vector<uint32_t> xrgb_yuv_packed_formats = {DRM_FORMAT_XRGB8888,
                                                   DRM_FORMAT_UYVY};
  ui::FakePlaneInfo primary_plane_info(100, 1 << 0, xrgb_yuv_packed_formats);
  ui::FakePlaneInfo overlay_info(101, 1 << 0,
                                 std::vector<uint32_t>(1, DRM_FORMAT_XRGB8888));
  std::vector<ui::FakePlaneInfo> planes_info{primary_plane_info, overlay_info};
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  std::vector<ui::OverlayCheckReturn_Params> returns =
      overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());
  ui::OverlayPlaneList plane_list =
      overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  EXPECT_EQ(DRM_FORMAT_XRGB8888,
            plane_list.back().buffer->GetFramebufferPixelFormat());
  EXPECT_EQ(3, plane_manager_->plane_count());
  for (const auto& param : returns)
    EXPECT_EQ(param.status, ui::OverlayCheckReturn_Params::Status::ABLE);
}

TEST_F(DrmOverlayValidatorTest, RejectYUVBuffersIfNotSupported) {
  plane_manager_->ResetPlaneCount();
  // Check case where buffer storage format is already UYVY but planes dont
  // support it.
  overlay_params_.back().buffer_size = overlay_rect_.size();
  overlay_params_.back().display_rect = overlay_rect_;
  plane_list_.back().display_bounds = overlay_rect_;

  std::vector<uint32_t> xrgb_yuv_packed_formats = {DRM_FORMAT_XRGB8888,
                                                   DRM_FORMAT_UYVY};
  ui::FakePlaneInfo primary_plane_info(100, 1 << 0, xrgb_yuv_packed_formats);
  ui::FakePlaneInfo overlay_info(101, 1 << 0,
                                 std::vector<uint32_t>(1, DRM_FORMAT_XRGB8888));
  std::vector<ui::FakePlaneInfo> planes_info{primary_plane_info, overlay_info};
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  std::vector<ui::OverlayCheck_Params> validated_params = overlay_params_;
  validated_params.back().format = gfx::BufferFormat::UYVY_422;
  plane_manager_->ResetPlaneCount();
  std::vector<ui::OverlayCheckReturn_Params> returns =
      overlay_validator_->TestPageFlip(validated_params,
                                       ui::OverlayPlaneList());

  EXPECT_EQ(returns.back().status, ui::OverlayCheckReturn_Params::Status::NOT);
}

TEST_F(DrmOverlayValidatorTest,
       RejectYUVBuffersIfNotSupported_MirroredControllers) {
  std::vector<uint32_t> crtcs{kDefaultCrtc, kSecondaryCrtc};
  plane_manager_->SetCrtcInfo(crtcs);

  std::vector<uint32_t> only_rgb_format{DRM_FORMAT_XRGB8888};
  std::vector<uint32_t> xrgb_yuv_packed_formats{DRM_FORMAT_XRGB8888,
                                                DRM_FORMAT_UYVY};
  ui::HardwareDisplayController* controller = window_->GetController();
  controller->AddCrtc(std::unique_ptr<ui::CrtcController>(
      new ui::CrtcController(drm_.get(), kSecondaryCrtc, kSecondaryConnector)));
  ui::OverlayPlane plane1(scoped_refptr<ui::ScanoutBuffer>(
      new ui::MockScanoutBuffer(primary_rect_.size())));
  EXPECT_TRUE(controller->Modeset(plane1, kDefaultMode));

  gfx::RectF crop_rect = gfx::RectF(0, 0, 0.5, 0.5);
  overlay_params_.back().buffer_size = overlay_rect_.size();
  overlay_params_.back().display_rect = overlay_rect_;
  overlay_params_.back().crop_rect = crop_rect;
  plane_list_.back().display_bounds = overlay_rect_;
  plane_list_.back().crop_rect = crop_rect;

  ui::FakePlaneInfo primary_crtc_primary_plane(100, 1 << 0, only_rgb_format);
  ui::FakePlaneInfo primary_crtc_overlay(101, 1 << 0, xrgb_yuv_packed_formats);
  ui::FakePlaneInfo secondary_crtc_primary_plane(102, 1 << 1, only_rgb_format);
  ui::FakePlaneInfo secondary_crtc_overlay(103, 1 << 1,
                                           xrgb_yuv_packed_formats);

  std::vector<ui::FakePlaneInfo> planes_info{
      primary_crtc_primary_plane, primary_crtc_overlay,
      secondary_crtc_primary_plane, secondary_crtc_overlay};
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  std::vector<ui::OverlayCheck_Params> validated_params = overlay_params_;
  validated_params.back().format = gfx::BufferFormat::UYVY_422;
  plane_manager_->ResetPlaneCount();
  std::vector<ui::OverlayCheckReturn_Params> returns =
      overlay_validator_->TestPageFlip(validated_params,
                                       ui::OverlayPlaneList());

  EXPECT_EQ(returns.back().status, ui::OverlayCheckReturn_Params::Status::ABLE);

  // Both controllers have Overlay which support DRM_FORMAT_UYVY, and scaling is
  // needed, hence this should be picked as the optimal format.
  ui::OverlayPlaneList plane_list =
      overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  EXPECT_EQ(DRM_FORMAT_UYVY,
            plane_list.back().buffer->GetFramebufferPixelFormat());

  // This configuration should not be promoted to Overlay when either of the
  // controllers dont support UYVY format.

  // Check case where we dont have support for packed formats in Mirrored CRTC.
  planes_info.back().allowed_formats = only_rgb_format;
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  returns = overlay_validator_->TestPageFlip(validated_params,
                                             ui::OverlayPlaneList());
  EXPECT_EQ(returns.back().status, ui::OverlayCheckReturn_Params::Status::NOT);

  // Check case where we dont have support for packed formats in primary
  // display.
  planes_info.back().allowed_formats = xrgb_yuv_packed_formats;
  planes_info[1].allowed_formats = only_rgb_format;
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  returns = overlay_validator_->TestPageFlip(validated_params,
                                             ui::OverlayPlaneList());
  EXPECT_EQ(returns.back().status, ui::OverlayCheckReturn_Params::Status::NOT);
  controller->RemoveCrtc(drm_, kSecondaryCrtc);
}

TEST_F(DrmOverlayValidatorTest, OptimalFormatYUV_MirroredControllers) {
  std::vector<uint32_t> crtcs{kDefaultCrtc, kSecondaryCrtc};
  plane_manager_->SetCrtcInfo(crtcs);
  overlay_validator_->ClearCache();

  ui::HardwareDisplayController* controller = window_->GetController();
  controller->AddCrtc(std::unique_ptr<ui::CrtcController>(
      new ui::CrtcController(drm_.get(), kSecondaryCrtc, kSecondaryConnector)));
  ui::OverlayPlane plane1(scoped_refptr<ui::ScanoutBuffer>(
      new ui::MockScanoutBuffer(primary_rect_.size())));
  EXPECT_TRUE(controller->Modeset(plane1, kDefaultMode));

  overlay_params_.back().buffer_size = overlay_rect_.size();
  overlay_params_.back().display_rect = overlay_rect_;
  plane_list_.back().display_bounds = overlay_rect_;
  std::vector<uint32_t> only_rgb_format = {DRM_FORMAT_XRGB8888};
  std::vector<uint32_t> xrgb_yuv_packed_formats = {DRM_FORMAT_XRGB8888,
                                                   DRM_FORMAT_UYVY};

  ui::FakePlaneInfo primary_crtc_primary_plane(100, 1 << 0, only_rgb_format);
  ui::FakePlaneInfo primary_crtc_overlay(101, 1 << 0, xrgb_yuv_packed_formats);
  ui::FakePlaneInfo secondary_crtc_primary_plane(102, 1 << 1, only_rgb_format);
  ui::FakePlaneInfo secondary_crtc_overlay(103, 1 << 1,
                                           xrgb_yuv_packed_formats);

  std::vector<ui::FakePlaneInfo> planes_info{
      primary_crtc_primary_plane, primary_crtc_overlay,
      secondary_crtc_primary_plane, secondary_crtc_overlay};
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  plane_manager_->ResetPlaneCount();
  std::vector<ui::OverlayCheckReturn_Params> returns =
      overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());

  EXPECT_EQ(returns.back().status, ui::OverlayCheckReturn_Params::Status::ABLE);
  ui::OverlayPlaneList plane_list =
      overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  EXPECT_EQ(DRM_FORMAT_XRGB8888,
            plane_list.back().buffer->GetFramebufferPixelFormat());

  // Check case where we dont have support for packed formats in Mirrored CRTC.
  planes_info.back().allowed_formats = only_rgb_format;
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  returns =
      overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());
  EXPECT_EQ(returns.back().status, ui::OverlayCheckReturn_Params::Status::ABLE);

  plane_list = overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  EXPECT_EQ(DRM_FORMAT_XRGB8888,
            plane_list.back().buffer->GetFramebufferPixelFormat());

  // Check case where we dont have support for packed formats in primary
  // display.
  planes_info.back().allowed_formats = xrgb_yuv_packed_formats;
  planes_info[1].allowed_formats = only_rgb_format;
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  returns =
      overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());
  EXPECT_EQ(returns.back().status, ui::OverlayCheckReturn_Params::Status::ABLE);

  plane_list = overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  EXPECT_EQ(DRM_FORMAT_XRGB8888,
            plane_list.back().buffer->GetFramebufferPixelFormat());
  controller->RemoveCrtc(drm_, kSecondaryCrtc);
}

TEST_F(DrmOverlayValidatorTest, OptimizeOnlyIfProcessingCallbackPresent) {
  // This test checks that we dont manipulate overlay buffers in case Processing
  // callback is not present.
  gfx::RectF crop_rect = gfx::RectF(0, 0, 0.5, 0.5);
  overlay_params_.back().buffer_size = overlay_rect_.size();
  overlay_params_.back().display_rect = overlay_rect_;
  overlay_params_.back().crop_rect = crop_rect;
  plane_list_.back().display_bounds = overlay_rect_;
  plane_list_.back().crop_rect = crop_rect;
  std::vector<uint32_t> xrgb_yuv_packed_formats = {DRM_FORMAT_XRGB8888,
                                                   DRM_FORMAT_UYVY};

  ui::FakePlaneInfo primary_plane_info(
      100, 1 << 0, std::vector<uint32_t>(1, DRM_FORMAT_XRGB8888));
  ui::FakePlaneInfo overlay_info(101, 1 << 0, xrgb_yuv_packed_formats);
  std::vector<ui::FakePlaneInfo> planes_info{primary_plane_info, overlay_info};
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());

  ui::OverlayPlaneList plane_list =
      overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  // Scaling allows format conversion.
  EXPECT_EQ(DRM_FORMAT_UYVY,
            plane_list.back().buffer->GetFramebufferPixelFormat());
  plane_list_.back().processing_callback.Reset();
  plane_list = overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  EXPECT_EQ(plane_list_.back().buffer->GetFramebufferPixelFormat(),
            plane_list.back().buffer->GetFramebufferPixelFormat());
  plane_list_.back().processing_callback = process_buffer_handler_;
}

TEST_F(DrmOverlayValidatorTest, DontResetOriginalBufferIfProcessedIsInvalid) {
  // This test checks that we dont manipulate overlay buffers in case Processing
  // callback is not present.
  gfx::RectF crop_rect = gfx::RectF(0, 0, 0.5, 0.5);
  overlay_params_.back().buffer_size = overlay_rect_.size();
  overlay_params_.back().display_rect = overlay_rect_;
  overlay_params_.back().crop_rect = crop_rect;
  plane_list_.back().display_bounds = overlay_rect_;
  plane_list_.back().crop_rect = crop_rect;
  std::vector<uint32_t> xrgb_yuv_packed_formats = {DRM_FORMAT_XRGB8888,
                                                   DRM_FORMAT_UYVY};

  ui::FakePlaneInfo primary_plane_info(
      100, 1 << 0, std::vector<uint32_t>(1, DRM_FORMAT_XRGB8888));
  ui::FakePlaneInfo overlay_info(101, 1 << 0, xrgb_yuv_packed_formats);
  std::vector<ui::FakePlaneInfo> planes_info{primary_plane_info, overlay_info};
  plane_manager_->SetPlaneProperties(planes_info);
  overlay_validator_->ClearCache();

  overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());

  ui::OverlayPlaneList plane_list =
      overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  // Scaling allows format conversion.
  EXPECT_EQ(DRM_FORMAT_UYVY,
            plane_list.back().buffer->GetFramebufferPixelFormat());
  plane_list_.back().processing_callback = base::Bind(
      &DrmOverlayValidatorTest::ReturnNullBuffer, base::Unretained(this));

  plane_list = overlay_validator_->PrepareBuffersForPageFlip(plane_list_);
  EXPECT_EQ(plane_list_.back().buffer->GetFramebufferPixelFormat(),
            plane_list.back().buffer->GetFramebufferPixelFormat());
  plane_list_.back().processing_callback = base::Bind(
      &DrmOverlayValidatorTest::ProcessBuffer, base::Unretained(this));
}

TEST_F(DrmOverlayValidatorTest, RejectBufferAllocationFail) {
  // Buffer allocation for scanout might fail.
  // In that case we should reject the overlay candidate.
  buffer_generator_->set_allocation_failure(true);

  std::vector<ui::OverlayCheckReturn_Params> returns =
      overlay_validator_->TestPageFlip(overlay_params_, ui::OverlayPlaneList());
  EXPECT_EQ(returns.front().status, ui::OverlayCheckReturn_Params::Status::NOT);
}

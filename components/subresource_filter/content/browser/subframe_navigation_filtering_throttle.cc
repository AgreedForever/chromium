// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/subresource_filter/content/browser/subframe_navigation_filtering_throttle.h"

#include <sstream>

#include "base/bind.h"
#include "base/logging.h"
#include "base/metrics/histogram_macros.h"
#include "components/subresource_filter/content/browser/subresource_filter_observer_manager.h"
#include "components/subresource_filter/core/browser/subresource_filter_constants.h"
#include "components/subresource_filter/core/common/time_measurements.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/browser_side_navigation_policy.h"
#include "content/public/common/console_message_level.h"

namespace subresource_filter {

SubframeNavigationFilteringThrottle::SubframeNavigationFilteringThrottle(
    content::NavigationHandle* handle,
    AsyncDocumentSubresourceFilter* parent_frame_filter)
    : content::NavigationThrottle(handle),
      parent_frame_filter_(parent_frame_filter),
      weak_ptr_factory_(this) {
  DCHECK(!handle->IsInMainFrame());
  DCHECK(parent_frame_filter_);
}

SubframeNavigationFilteringThrottle::~SubframeNavigationFilteringThrottle() {
  switch (load_policy_) {
    case LoadPolicy::ALLOW:
      UMA_HISTOGRAM_CUSTOM_MICRO_TIMES(
          "SubresourceFilter.DocumentLoad.SubframeFilteringDelay.Allowed",
          total_defer_time_, base::TimeDelta::FromMicroseconds(1),
          base::TimeDelta::FromSeconds(10), 50);
      break;
    case LoadPolicy::WOULD_DISALLOW:
    // fall through
    case LoadPolicy::DISALLOW:
      UMA_HISTOGRAM_CUSTOM_MICRO_TIMES(
          "SubresourceFilter.DocumentLoad.SubframeFilteringDelay.Disallowed",
          total_defer_time_, base::TimeDelta::FromMicroseconds(1),
          base::TimeDelta::FromSeconds(10), 50);
      break;
  }
}

content::NavigationThrottle::ThrottleCheckResult
SubframeNavigationFilteringThrottle::WillStartRequest() {
  return DeferToCalculateLoadPolicy(ThrottlingStage::WillStartRequest);
}

content::NavigationThrottle::ThrottleCheckResult
SubframeNavigationFilteringThrottle::WillRedirectRequest() {
  return DeferToCalculateLoadPolicy(ThrottlingStage::WillRedirectRequest);
}

content::NavigationThrottle::ThrottleCheckResult
SubframeNavigationFilteringThrottle::WillProcessResponse() {
  DCHECK_NE(load_policy_, LoadPolicy::DISALLOW);
  NotifyLoadPolicy();
  return content::NavigationThrottle::ThrottleCheckResult::PROCEED;
}

const char* SubframeNavigationFilteringThrottle::GetNameForLogging() {
  return "SubframeNavigationFilteringThrottle";
}

content::NavigationThrottle::ThrottleCheckResult
SubframeNavigationFilteringThrottle::DeferToCalculateLoadPolicy(
    ThrottlingStage stage) {
  DCHECK_NE(load_policy_, LoadPolicy::DISALLOW);
  if (load_policy_ == LoadPolicy::WOULD_DISALLOW)
    return content::NavigationThrottle::ThrottleCheckResult::PROCEED;
  parent_frame_filter_->GetLoadPolicyForSubdocument(
      navigation_handle()->GetURL(),
      base::Bind(&SubframeNavigationFilteringThrottle::OnCalculatedLoadPolicy,
                 weak_ptr_factory_.GetWeakPtr(), stage));
  last_defer_timestamp_ = base::TimeTicks::Now();
  return content::NavigationThrottle::ThrottleCheckResult::DEFER;
}

void SubframeNavigationFilteringThrottle::OnCalculatedLoadPolicy(
    ThrottlingStage stage,
    LoadPolicy policy) {
  DCHECK(!last_defer_timestamp_.is_null());
  load_policy_ = policy;
  total_defer_time_ += base::TimeTicks::Now() - last_defer_timestamp_;

  if (policy == LoadPolicy::DISALLOW) {
    if (parent_frame_filter_->activation_state().enable_logging) {
      std::ostringstream oss(kDisallowSubframeConsoleMessage);
      oss << navigation_handle()->GetURL() << ".";
      navigation_handle()
          ->GetWebContents()
          ->GetMainFrame()
          ->AddMessageToConsole(content::CONSOLE_MESSAGE_LEVEL_ERROR,
                                oss.str());
    }

    parent_frame_filter_->ReportDisallowedLoad();
    // Other load policies will be reported in WillProcessResponse.
    NotifyLoadPolicy();

    const bool block_and_collapse_is_supported =
        content::IsBrowserSideNavigationEnabled() ||
        stage == ThrottlingStage::WillStartRequest;
    navigation_handle()->CancelDeferredNavigation(
        block_and_collapse_is_supported
            ? content::NavigationThrottle::BLOCK_REQUEST_AND_COLLAPSE
            : content::NavigationThrottle::CANCEL);
  } else {
    navigation_handle()->Resume();
  }
}

void SubframeNavigationFilteringThrottle::NotifyLoadPolicy() const {
  if (auto* observer_manager =
          SubresourceFilterObserverManager::FromWebContents(
              navigation_handle()->GetWebContents())) {
    observer_manager->NotifySubframeNavigationEvaluated(navigation_handle(),
                                                        load_policy_);
  }
}

}  // namespace subresource_filter

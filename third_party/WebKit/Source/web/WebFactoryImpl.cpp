// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "web/WebFactoryImpl.h"
#include "web/ChromeClientImpl.h"
#include "web/WebLocalFrameImpl.h"
#include "web/WebViewImpl.h"

namespace blink {

void WebFactoryImpl::Initialize() {
  WebFactory::SetInstance(*(new WebFactoryImpl()));
}

ChromeClient* WebFactoryImpl::CreateChromeClient(WebViewBase* view) const {
  return ChromeClientImpl::Create(view);
}

WebViewBase* WebFactoryImpl::CreateWebViewBase(
    WebViewClient* client,
    WebPageVisibilityState state) const {
  return WebViewImpl::Create(client, state);
}

WebLocalFrameBase* WebFactoryImpl::CreateWebLocalFrameBase(
    WebTreeScopeType type,
    WebFrameClient* client,
    blink::InterfaceProvider* provider,
    blink::InterfaceRegistry* registry,
    WebFrame* opener) const {
  return WebLocalFrameImpl::Create(type, client, provider, registry, opener);
}
}

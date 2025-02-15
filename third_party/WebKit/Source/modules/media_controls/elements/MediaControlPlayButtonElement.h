// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MediaControlPlayButtonElement_h
#define MediaControlPlayButtonElement_h

#include "core/html/shadow/MediaControlElementTypes.h"

namespace blink {

class Event;
class MediaControlsImpl;

class MediaControlPlayButtonElement final : public MediaControlInputElement {
 public:
  explicit MediaControlPlayButtonElement(MediaControlsImpl&);

  // MediaControlInputElement overrides.
  bool WillRespondToMouseClickEvents() override;
  void UpdateDisplayType() override;
  WebLocalizedString::Name GetOverflowStringName() override;
  bool HasOverflowButton() override;

  void OnMediaKeyboardEvent(Event* event) { DefaultEventHandler(event); }

 private:
  void DefaultEventHandler(Event*) override;
};

}  // namespace blink

#endif  // MediaControlPlayButtonElement_h

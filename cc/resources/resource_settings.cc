// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/resources/resource_settings.h"

namespace cc {

ResourceSettings::ResourceSettings() = default;

ResourceSettings::ResourceSettings(const ResourceSettings& other) = default;

ResourceSettings::~ResourceSettings() = default;

ResourceSettings& ResourceSettings::operator=(const ResourceSettings& other) =
    default;

}  // namespace cc

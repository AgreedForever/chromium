// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/process/process_info.h"

#include <windows.h>

#include "base/logging.h"
#include "base/memory/ptr_util.h"
#include "base/time/time.h"
#include "base/win/scoped_handle.h"

namespace base {

// static
const Time CurrentProcessInfo::CreationTime() {
  FILETIME creation_time = {};
  FILETIME ignore1 = {};
  FILETIME ignore2 = {};
  FILETIME ignore3 = {};
  if (!::GetProcessTimes(::GetCurrentProcess(), &creation_time, &ignore1,
                         &ignore2, &ignore3)) {
    return Time();
  }
  return Time::FromFileTime(creation_time);
}

IntegrityLevel GetCurrentProcessIntegrityLevel() {
  HANDLE process_token;
  if (!::OpenProcessToken(::GetCurrentProcess(),
                          TOKEN_QUERY | TOKEN_QUERY_SOURCE, &process_token)) {
    return INTEGRITY_UNKNOWN;
  }
  win::ScopedHandle scoped_process_token(process_token);

  DWORD token_info_length = 0;
  if (::GetTokenInformation(process_token, TokenIntegrityLevel, nullptr, 0,
                            &token_info_length) ||
      ::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    return INTEGRITY_UNKNOWN;
  }

  auto token_label_bytes = MakeUnique<char[]>(token_info_length);
  TOKEN_MANDATORY_LABEL* token_label =
      reinterpret_cast<TOKEN_MANDATORY_LABEL*>(token_label_bytes.get());
  if (!::GetTokenInformation(process_token, TokenIntegrityLevel, token_label,
                             token_info_length, &token_info_length)) {
    return INTEGRITY_UNKNOWN;
  }

  DWORD integrity_level = *::GetSidSubAuthority(
      token_label->Label.Sid,
      static_cast<DWORD>(*::GetSidSubAuthorityCount(token_label->Label.Sid) -
                         1));

  if (integrity_level < SECURITY_MANDATORY_MEDIUM_RID)
    return LOW_INTEGRITY;

  if (integrity_level >= SECURITY_MANDATORY_MEDIUM_RID &&
      integrity_level < SECURITY_MANDATORY_HIGH_RID) {
    return MEDIUM_INTEGRITY;
  }

  if (integrity_level >= SECURITY_MANDATORY_HIGH_RID)
    return HIGH_INTEGRITY;

  NOTREACHED();
  return INTEGRITY_UNKNOWN;
}

}  // namespace base

// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_FEEDBACK_SYSTEM_LOGS_LOG_SOURCES_CRASH_IDS_SOURCE_H_
#define CHROME_BROWSER_FEEDBACK_SYSTEM_LOGS_LOG_SOURCES_CRASH_IDS_SOURCE_H_

#include <vector>

#include "base/callback_forward.h"
#include "chrome/browser/feedback/system_logs/system_logs_fetcher.h"
#include "components/upload_list/crash_upload_list.h"
#include "components/upload_list/upload_list.h"

namespace system_logs {

// Extract the most recent crash IDs (if any) and adds them to the system logs.
class CrashIdsSource : public SystemLogsSource, public UploadList::Delegate {
 public:
  CrashIdsSource();
  ~CrashIdsSource() override;

  // SystemLogsSource:
  void Fetch(const SysLogsSourceCallback& callback) override;

  // UploadList::Delegate:
  void OnUploadListAvailable() override;

 private:
  void RespondWithCrashIds(const SysLogsSourceCallback& callback) const;

  scoped_refptr<CrashUploadList> crash_upload_list_;

  // A comma-separated list of crash IDs as expected by the server.
  std::string crash_ids_list_;

  // Contains any pending fetch requests waiting for the crash upload list to
  // finish loading.
  std::vector<base::Closure> pending_requests_;

  // True if the crash list is currently being loaded.
  bool pending_crash_list_loading_;

  DISALLOW_COPY_AND_ASSIGN(CrashIdsSource);
};

}  // namespace system_logs

#endif  // CHROME_BROWSER_FEEDBACK_SYSTEM_LOGS_LOG_SOURCES_CRASH_IDS_SOURCE_H_

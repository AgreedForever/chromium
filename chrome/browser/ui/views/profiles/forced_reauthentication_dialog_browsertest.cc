// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/profiles/forced_reauthentication_dialog.h"

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/signin/signin_manager_factory.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/test/test_browser_dialog.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/signin/core/browser/signin_manager.h"
#include "ui/base/ui_base_types.h"

class ForcedReauthenticationDialogBrowserTest : public DialogBrowserTest {
 public:
  ForcedReauthenticationDialogBrowserTest() {}

  // override DialogBrowserTest
  void ShowDialog(const std::string& name) override {
    Profile* profile = browser()->profile();
    SigninManager* manager = SigninManagerFactory::GetForProfile(profile);
    manager->SetAuthenticatedAccountInfo("test1", "test@xyz.com");
    ForcedReauthenticationDialog::ShowDialog(profile, manager,
                                             base::TimeDelta::FromSeconds(60));
  }

  // An integer represents the buttons of dialog.

 private:
  DISALLOW_COPY_AND_ASSIGN(ForcedReauthenticationDialogBrowserTest);
};

IN_PROC_BROWSER_TEST_F(ForcedReauthenticationDialogBrowserTest,
                       InvokeDialog_default) {
  RunDialog();
}

// Dialog will not be display if there is no valid browser window.
IN_PROC_BROWSER_TEST_F(ForcedReauthenticationDialogBrowserTest,
                       NotOpenDialogDueToNoBrowser) {
  Profile* profile = browser()->profile();
  CloseBrowserSynchronously(browser());
  EXPECT_EQ(nullptr, ForcedReauthenticationDialog::ShowDialog(
                         profile, SigninManagerFactory::GetForProfile(profile),
                         base::TimeDelta::FromSeconds(60)));
}

IN_PROC_BROWSER_TEST_F(ForcedReauthenticationDialogBrowserTest,
                       NotOpenDialogDueToNoTabs) {
  Profile* profile = browser()->profile();
  TabStripModel* model = browser()->tab_strip_model();
  ASSERT_EQ(1, model->count());
  model->CloseWebContentsAt(0, TabStripModel::CLOSE_NONE);
  ASSERT_TRUE(model->empty());
  EXPECT_EQ(nullptr, ForcedReauthenticationDialog::ShowDialog(
                         profile, SigninManagerFactory::GetForProfile(profile),
                         base::TimeDelta::FromSeconds(60)));
}

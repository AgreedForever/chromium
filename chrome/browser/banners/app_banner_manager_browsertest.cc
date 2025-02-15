// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/command_line.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/strings/string16.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/histogram_tester.h"
#include "base/test/scoped_feature_list.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/banners/app_banner_manager.h"
#include "chrome/browser/banners/app_banner_metrics.h"
#include "chrome/browser/banners/app_banner_settings_helper.h"
#include "chrome/browser/engagement/site_engagement_score.h"
#include "chrome/browser/engagement/site_engagement_service.h"
#include "chrome/browser/installable/installable_logging.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"

namespace banners {

// Browser tests for web app banners.
// NOTE: this test relies on service workers; failures and flakiness may be due
// to changes in SW code.
class AppBannerManagerTest : public AppBannerManager {
 public:
  explicit AppBannerManagerTest(content::WebContents* web_contents)
      : AppBannerManager(web_contents) {}

  ~AppBannerManagerTest() override {}

  void RequestAppBanner(const GURL& validated_url,
                        bool is_debug_mode) override {
    // Filter out about:blank navigations - we use these in testing to force
    // Stop() to be called.
    if (validated_url == GURL("about:blank"))
      return;

    AppBannerManager::RequestAppBanner(validated_url, is_debug_mode);
  }

  bool will_show() { return will_show_.get() && *will_show_; }

  void clear_will_show() { will_show_.reset(); }

  bool is_active() { return AppBannerManager::is_active(); }

  bool is_complete() { return AppBannerManager::is_complete(); }

  bool is_pending_engagement() {
    return AppBannerManager::is_pending_engagement();
  }

  bool need_to_log_status() { return need_to_log_status_; }

  void Prepare(base::Closure quit_closure) {
    quit_closure_ = quit_closure;
  }

 protected:
  // All calls to RequestAppBanner should terminate in one of Stop() (not
  // showing banner), UpdateState(State::PENDING_ENGAGEMENT) (waiting for
  // sufficient engagement), or ShowBanner(). Override these methods to capture
  // test status.
  void Stop() override {
    AppBannerManager::Stop();
    ASSERT_FALSE(will_show_.get());
    will_show_.reset(new bool(false));
    base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, quit_closure_);
  }

  void ShowBanner() override {
    // Fake the call to ReportStatus here - this is usually called in
    // platform-specific code which is not exposed here.
    ReportStatus(nullptr, SHOWING_WEB_APP_BANNER);
    RecordDidShowBanner("AppBanner.WebApp.Shown");

    ASSERT_FALSE(will_show_.get());
    will_show_.reset(new bool(true));
    base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, quit_closure_);
  }

  void UpdateState(AppBannerManager::State state) override {
    AppBannerManager::UpdateState(state);

    if (state == AppBannerManager::State::PENDING_ENGAGEMENT)
      base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, quit_closure_);
  }

 private:
  bool IsDebugMode() const override { return false; }

  base::Closure quit_closure_;
  std::unique_ptr<bool> will_show_;

  DISALLOW_COPY_AND_ASSIGN(AppBannerManagerTest);
};

class AppBannerManagerBrowserTest : public InProcessBrowserTest {
 public:
  void SetUpOnMainThread() override {
    AppBannerSettingsHelper::SetTotalEngagementToTrigger(10);
    SiteEngagementScore::SetParamValuesForTesting();
    ASSERT_TRUE(embedded_test_server()->Start());
    InProcessBrowserTest::SetUpOnMainThread();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    // Make sure app banners are disabled in the browser, otherwise they will
    // interfere with the test.
    command_line->AppendSwitch(switches::kDisableAddToShelf);
  }

 protected:
  // Returns a test server URL to page |page_url| with |manifest_url| injected
  // as the manifest tag.
  std::string GetURLOfPageWithManifest(const std::string& page_url,
                                       const std::string& manifest_url) {
    return page_url + embedded_test_server()->GetURL(manifest_url).spec();
  }

  // Returns a test server URL to a page controlled by a service worker with
  // |manifest_url| injected as the manifest tag.
  std::string GetURLOfPageWithServiceWorkerAndManifest(
      const std::string& manifest_url) {
    return GetURLOfPageWithManifest(
        "/banners/manifest_test_page.html?manifest=", manifest_url);
  }

  std::unique_ptr<AppBannerManagerTest> CreateAppBannerManager(
      Browser* browser) {
    content::WebContents* web_contents =
        browser->tab_strip_model()->GetActiveWebContents();
    return base::MakeUnique<AppBannerManagerTest>(web_contents);
  }

  void RunBannerTest(Browser* browser,
                     AppBannerManagerTest* manager,
                     const std::string& url,
                     const std::vector<double>& engagement_scores,
                     InstallableStatusCode expected_code_for_histogram,
                     bool expected_to_show) {
    RunBannerTest(browser, manager, url, engagement_scores,
                  expected_code_for_histogram, expected_to_show,
                  base::string16());
  }

  void RunBannerTest(Browser* browser,
                     AppBannerManagerTest* manager,
                     const std::string& url,
                     const std::vector<double>& engagement_scores,
                     InstallableStatusCode expected_code_for_histogram,
                     bool expected_to_show,
                     const base::string16 expected_tab_title) {
    base::HistogramTester histograms;
    GURL test_url = embedded_test_server()->GetURL(url);

    manager->clear_will_show();

    // Loop through the vector of engagement scores. We only expect the banner
    // pipeline to trigger on the last one; otherwise, nothing is expected to
    // happen.
    int iterations = 0;
    SiteEngagementService* service =
        SiteEngagementService::Get(browser->profile());
    for (double engagement : engagement_scores) {
      if (iterations > 0) {
        ui_test_utils::NavigateToURL(browser, test_url);

        EXPECT_FALSE(manager->will_show());
        EXPECT_FALSE(manager->is_active());

        histograms.ExpectTotalCount(banners::kMinutesHistogram, 0);
        histograms.ExpectTotalCount(banners::kInstallableStatusCodeHistogram,
                                    0);
      }
      service->ResetBaseScoreForURL(test_url, engagement);
      ++iterations;
    }

    // On the final loop, we expect the banner pipeline to trigger - the
    // navigation should generate the final engagement to show the banner. Spin
    // the run loop and wait for the manager to finish.
    base::RunLoop run_loop;
    manager->clear_will_show();
    manager->Prepare(run_loop.QuitClosure());
    ui_test_utils::NavigateToURL(browser, test_url);
    run_loop.Run();

    EXPECT_EQ(expected_to_show, manager->will_show());
    EXPECT_FALSE(manager->is_active());

    // Check the tab title; this allows the test page to send data back out to
    // be inspected by the test case.
    if (!expected_tab_title.empty()) {
      base::string16 title;
      EXPECT_TRUE(ui_test_utils::GetCurrentTabTitle(browser, &title));
      EXPECT_EQ(expected_tab_title, title);
    }

    // If in incognito, ensure that nothing is recorded.
    // If showing the banner, ensure that the minutes histogram is recorded.
    if (browser->profile()->IsOffTheRecord()) {
      histograms.ExpectTotalCount(banners::kMinutesHistogram, 0);
      histograms.ExpectTotalCount(banners::kInstallableStatusCodeHistogram, 0);
    } else {
      histograms.ExpectTotalCount(banners::kMinutesHistogram,
                                  (manager->will_show() ? 1 : 0));
      histograms.ExpectUniqueSample(banners::kInstallableStatusCodeHistogram,
                                    expected_code_for_histogram, 1);
      EXPECT_FALSE(manager->need_to_log_status());
    }
  }
};

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest, WebAppBannerCreated) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{0, 10};
  RunBannerTest(browser(), manager.get(), "/banners/manifest_test_page.html",
                engagement_scores, SHOWING_WEB_APP_BANNER, true);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest,
                       WebAppBannerCreatedImmediately) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{10};
  RunBannerTest(browser(), manager.get(), "/banners/manifest_test_page.html",
                engagement_scores, SHOWING_WEB_APP_BANNER, true);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest,
                       WebAppBannerCreatedAfterSeveralVisits) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{0, 1, 2, 3, 4, 5, 10};
  RunBannerTest(browser(), manager.get(), "/banners/manifest_test_page.html",
                engagement_scores, SHOWING_WEB_APP_BANNER, true);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest,
                       WebAppBannerNotSeenAfterShowing) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{0, 10};
  RunBannerTest(browser(), manager.get(), "/banners/manifest_test_page.html",
                engagement_scores, SHOWING_WEB_APP_BANNER, true);

  AppBannerManager::SetTimeDeltaForTesting(1);
  RunBannerTest(browser(), manager.get(), "/banners/manifest_test_page.html",
                engagement_scores, PREVIOUSLY_IGNORED, false);

  AppBannerManager::SetTimeDeltaForTesting(13);
  RunBannerTest(browser(), manager.get(), "/banners/manifest_test_page.html",
                engagement_scores, PREVIOUSLY_IGNORED, false);

  AppBannerManager::SetTimeDeltaForTesting(14);
  RunBannerTest(browser(), manager.get(), "/banners/manifest_test_page.html",
                engagement_scores, SHOWING_WEB_APP_BANNER, true);

  AppBannerSettingsHelper::SetDaysAfterDismissAndIgnoreToTrigger(90, 2);

  AppBannerManager::SetTimeDeltaForTesting(16);
  RunBannerTest(browser(), manager.get(), "/banners/manifest_test_page.html",
                engagement_scores, SHOWING_WEB_APP_BANNER, true);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest,
                       WebAppBannerNoTypeInManifest) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{0, 10};
  RunBannerTest(browser(), manager.get(),
                GetURLOfPageWithServiceWorkerAndManifest(
                    "/banners/manifest_no_type.json"),
                engagement_scores, SHOWING_WEB_APP_BANNER, true);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest,
                       WebAppBannerNoTypeInManifestCapsExtension) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{0, 10};
  RunBannerTest(browser(), manager.get(),
                GetURLOfPageWithServiceWorkerAndManifest(
                    "/banners/manifest_no_type_caps.json"),
                engagement_scores, SHOWING_WEB_APP_BANNER, true);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest, NoManifest) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{10};
  RunBannerTest(browser(), manager.get(), "/banners/no_manifest_test_page.html",
                engagement_scores, NO_MANIFEST, false);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest, MissingManifest) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{10};
  RunBannerTest(browser(), manager.get(),
                GetURLOfPageWithServiceWorkerAndManifest(
                    "/banners/manifest_missing.json"),
                engagement_scores, MANIFEST_EMPTY, false);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest, BeforeInstallPrompt) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{0, 5, 10};

  // Expect that the page sets the tab title to indicate that it got the event
  // twice: once for addEventListener('beforeinstallprompt'), and once for the
  // onbeforeinstallprompt attribute.
  RunBannerTest(browser(), manager.get(),
                "/banners/beforeinstallprompt_test_page.html",
                engagement_scores, SHOWING_WEB_APP_BANNER, true,
                base::ASCIIToUTF16("Got beforeinstallprompt: listener, attr"));
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest, CancelBannerDirect) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{10};
  RunBannerTest(browser(), manager.get(), "/banners/cancel_test_page.html",
                engagement_scores, RENDERER_CANCELLED, false);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest, PromptBanner) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{0, 5, 10};
  RunBannerTest(browser(), manager.get(), "/banners/prompt_test_page.html",
                engagement_scores, SHOWING_WEB_APP_BANNER, true);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest, PromptBannerInHandler) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{0, 2, 5, 10};
  RunBannerTest(browser(), manager.get(),
                "/banners/prompt_in_handler_test_page.html", engagement_scores,
                SHOWING_WEB_APP_BANNER, true);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest,
                       CancelBannerAfterPromptInHandler) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{10};
  RunBannerTest(browser(), manager.get(),
                "/banners/prompt_in_handler_test_page.html", engagement_scores,
                SHOWING_WEB_APP_BANNER, true);
  std::string cancel_test_page_url =
      GetURLOfPageWithManifest("/banners/cancel_test_page.html?manifest=",
                               "/banners/manifest_different_start_url.json");
  RunBannerTest(browser(), manager.get(), cancel_test_page_url,
                engagement_scores, RENDERER_CANCELLED, false);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest, WebAppBannerInIFrame) {
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{10};
  RunBannerTest(browser(), manager.get(), "/banners/iframe_test_page.html",
                engagement_scores, NO_MANIFEST, false);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest, DoesNotShowInIncognito) {
  Browser* incognito_browser =
      OpenURLOffTheRecord(browser()->profile(), GURL("about:blank"));
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(incognito_browser));
  std::vector<double> engagement_scores{10};
  RunBannerTest(incognito_browser, manager.get(),
                "/banners/manifest_test_page.html", engagement_scores,
                IN_INCOGNITO, false);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest,
                       CheckOnLoadWithSufficientEngagement) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kCheckInstallabilityForBannerOnLoad);
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{10};
  RunBannerTest(browser(), manager.get(), "/banners/manifest_test_page.html",
                engagement_scores, SHOWING_WEB_APP_BANNER, true);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest,
                       CheckOnLoadWithSufficientEngagementCancelDirect) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kCheckInstallabilityForBannerOnLoad);
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{10};
  RunBannerTest(browser(), manager.get(), "/banners/cancel_test_page.html",
                engagement_scores, RENDERER_CANCELLED, false);
}

IN_PROC_BROWSER_TEST_F(
    AppBannerManagerBrowserTest,
    CheckOnLoadWithSufficientEngagementCancelBannerAfterPromptInHandler) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kCheckInstallabilityForBannerOnLoad);
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));
  std::vector<double> engagement_scores{10};
  RunBannerTest(browser(), manager.get(),
                "/banners/prompt_in_handler_test_page.html", engagement_scores,
                SHOWING_WEB_APP_BANNER, true);
  std::string cancel_test_page_url =
      GetURLOfPageWithManifest("/banners/cancel_test_page.html?manifest=",
                               "/banners/manifest_different_start_url.json");
  RunBannerTest(browser(), manager.get(), cancel_test_page_url,
                engagement_scores, RENDERER_CANCELLED, false);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest,
                       CheckOnLoadWithoutSufficientEngagement) {
  AppBannerSettingsHelper::SetTotalEngagementToTrigger(1);
  SiteEngagementService* service =
      SiteEngagementService::Get(browser()->profile());
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kCheckInstallabilityForBannerOnLoad);
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));

  base::HistogramTester histograms;
  GURL test_url =
      embedded_test_server()->GetURL("/banners/manifest_test_page.html");
  service->ResetBaseScoreForURL(test_url, 0);

  // First run through: expect the manager to end up stopped in the pending
  // state, without showing a banner.
  {
    base::RunLoop run_loop;
    manager->clear_will_show();
    manager->Prepare(run_loop.QuitClosure());
    ui_test_utils::NavigateToURL(browser(), test_url);
    run_loop.Run();
  }

  EXPECT_FALSE(manager->will_show());
  EXPECT_FALSE(manager->is_active());
  EXPECT_TRUE(manager->is_pending_engagement());
  EXPECT_TRUE(manager->need_to_log_status());

  // Trigger an engagement increase that signals observers and expect the banner
  // to be shown.
  {
    base::RunLoop run_loop;
    manager->clear_will_show();
    manager->Prepare(run_loop.QuitClosure());
    service->HandleNavigation(
        browser()->tab_strip_model()->GetActiveWebContents(),
        ui::PageTransition::PAGE_TRANSITION_TYPED);
    run_loop.Run();
  }

  EXPECT_TRUE(manager->will_show());
  EXPECT_FALSE(manager->is_active());
  EXPECT_FALSE(manager->need_to_log_status());
  EXPECT_TRUE(manager->is_complete());

  histograms.ExpectTotalCount(banners::kMinutesHistogram, 1);
  histograms.ExpectUniqueSample(banners::kInstallableStatusCodeHistogram,
                                SHOWING_WEB_APP_BANNER, 1);
}

IN_PROC_BROWSER_TEST_F(AppBannerManagerBrowserTest, CheckOnLoadThenNavigate) {
  base::test::ScopedFeatureList feature_list;
  feature_list.InitAndEnableFeature(
      features::kCheckInstallabilityForBannerOnLoad);
  std::unique_ptr<AppBannerManagerTest> manager(
      CreateAppBannerManager(browser()));

  base::HistogramTester histograms;
  GURL test_url =
      embedded_test_server()->GetURL("/banners/manifest_test_page.html");

  // First run through: expect the manager to end up stopped in the pending
  // state, without showing a banner.
  {
    base::RunLoop run_loop;
    manager->clear_will_show();
    manager->Prepare(run_loop.QuitClosure());
    ui_test_utils::NavigateToURL(browser(), test_url);
    run_loop.Run();
  }

  EXPECT_FALSE(manager->will_show());
  EXPECT_FALSE(manager->is_active());
  EXPECT_TRUE(manager->is_pending_engagement());
  EXPECT_TRUE(manager->need_to_log_status());

  // Navigate and expect Stop() to be called.
  {
    base::RunLoop run_loop;
    manager->clear_will_show();
    manager->Prepare(run_loop.QuitClosure());
    ui_test_utils::NavigateToURL(browser(), GURL("about:blank"));
    run_loop.Run();
  }

  EXPECT_FALSE(manager->will_show());
  EXPECT_FALSE(manager->is_active());
  EXPECT_FALSE(manager->need_to_log_status());

  histograms.ExpectTotalCount(banners::kMinutesHistogram, 0);
  histograms.ExpectUniqueSample(banners::kInstallableStatusCodeHistogram,
                                INSUFFICIENT_ENGAGEMENT, 1);
}

}  // namespace banners

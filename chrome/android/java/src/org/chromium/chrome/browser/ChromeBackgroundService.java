// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser;

import android.content.Context;

import com.google.android.gms.gcm.GcmNetworkManager;
import com.google.android.gms.gcm.GcmTaskService;
import com.google.android.gms.gcm.TaskParams;

import org.chromium.base.Log;
import org.chromium.base.ThreadUtils;
import org.chromium.base.VisibleForTesting;
import org.chromium.base.annotations.SuppressFBWarnings;
import org.chromium.base.library_loader.ProcessInitException;
import org.chromium.chrome.browser.download.DownloadResumptionScheduler;
import org.chromium.chrome.browser.init.ChromeBrowserInitializer;
import org.chromium.chrome.browser.ntp.snippets.SnippetsBridge;
import org.chromium.chrome.browser.ntp.snippets.SnippetsLauncher;
import org.chromium.chrome.browser.offlinepages.BackgroundScheduler;
import org.chromium.chrome.browser.offlinepages.OfflinePageUtils;
import org.chromium.chrome.browser.precache.PrecacheController;
import org.chromium.chrome.browser.precache.PrecacheUMA;

/**
 * {@link ChromeBackgroundService} is scheduled through the {@link GcmNetworkManager} when the
 * browser needs to be launched for scheduled tasks, or in response to changing network or power
 * conditions.
 */
public class ChromeBackgroundService extends GcmTaskService {
    private static final String TAG = "BackgroundService";

    @Override
    @VisibleForTesting
    public int onRunTask(final TaskParams params) {
        final String taskTag = params.getTag();
        Log.i(TAG, "[" + taskTag + "] Woken up at " + new java.util.Date().toString());
        final Context context = this;
        ThreadUtils.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                switch (taskTag) {
                    case BackgroundSyncLauncher.TASK_TAG:
                        handleBackgroundSyncEvent(context, taskTag);
                        break;

                    case OfflinePageUtils.TASK_TAG:
                        // Offline pages are migrating to BackgroundTaskScheduler, therefore getting
                        // a task through ChromeBackgroundSerivce should cause a rescheduling using
                        // the new component.
                        rescheduleOfflinePages();
                        break;

                    case SnippetsLauncher.TASK_TAG_WIFI:
                    case SnippetsLauncher.TASK_TAG_FALLBACK:
                        handleFetchSnippets(context, taskTag);
                        break;

                    case PrecacheController.PERIODIC_TASK_TAG:
                    case PrecacheController.CONTINUATION_TASK_TAG:
                        handlePrecache(context, taskTag);
                        break;

                    case DownloadResumptionScheduler.TASK_TAG:
                        DownloadResumptionScheduler.getDownloadResumptionScheduler(
                                context.getApplicationContext()).handleDownloadResumption();
                        break;

                    default:
                        Log.i(TAG, "Unknown task tag " + taskTag);
                        break;
                }
            }
        });

        return GcmNetworkManager.RESULT_SUCCESS;
    }

    private void handleBackgroundSyncEvent(Context context, String tag) {
        if (!BackgroundSyncLauncher.hasInstance()) {
            // Start the browser. The browser's BackgroundSyncManager (for the active profile) will
            // start, check the network, and run any necessary sync events. This task runs with a
            // wake lock, but has a three minute timeout, so we need to start the browser in its
            // own task.
            // TODO(jkarlin): Protect the browser sync event with a wake lock.
            // See crbug.com/486020.
            launchBrowser(context, tag);
        }
    }

    private void handleFetchSnippets(Context context, String tag) {
        if (!SnippetsLauncher.hasInstance()) {
            launchBrowser(context, tag);
        }
        fetchSnippets();
    }

    @VisibleForTesting
    protected void fetchSnippets() {
        SnippetsBridge.fetchRemoteSuggestionsFromBackground();
    }

    @VisibleForTesting
    protected void rescheduleFetching() {
        SnippetsBridge.rescheduleFetching();
    }

    private void handlePrecache(Context context, String tag) {
        if (!hasPrecacheInstance()) {
            launchBrowser(context, tag);
        }
        precache(context, tag);
    }

    @VisibleForTesting
    protected boolean hasPrecacheInstance() {
        return PrecacheController.hasInstance();
    }

    @VisibleForTesting
    protected void precache(Context context, String tag) {
        PrecacheController.get(context).precache(tag);
    }

    @VisibleForTesting
    @SuppressFBWarnings("DM_EXIT")
    protected void launchBrowser(Context context, String tag) {
        Log.i(TAG, "Launching browser");
        try {
            ChromeBrowserInitializer.getInstance(this).handleSynchronousStartup();
        } catch (ProcessInitException e) {
            Log.e(TAG, "ProcessInitException while starting the browser process");
            switch (tag) {
                case PrecacheController.PERIODIC_TASK_TAG:
                case PrecacheController.CONTINUATION_TASK_TAG:
                    // Record the failure persistently, and upload to UMA when the library
                    // successfully loads next time.
                    PrecacheUMA.record(PrecacheUMA.Event.PRECACHE_TASK_LOAD_LIBRARY_FAIL);
                    break;
                default:
                    break;
            }
            // Since the library failed to initialize nothing in the application
            // can work, so kill the whole application not just the activity.
            System.exit(-1);
        }
    }

    @VisibleForTesting
    protected void rescheduleBackgroundSyncTasksOnUpgrade() {
        BackgroundSyncLauncher.rescheduleTasksOnUpgrade(this);
    }

    @VisibleForTesting
    protected void reschedulePrecacheTasksOnUpgrade() {
        PrecacheController.rescheduleTasksOnUpgrade(this);
    }

    private void rescheduleSnippetsTasksOnUpgrade() {
        if (SnippetsLauncher.shouldRescheduleTasksOnUpgrade()) {
            if (!SnippetsLauncher.hasInstance()) {
                launchBrowser(this, /*tag=*/""); // The |tag| doesn't matter here.
            }
            rescheduleFetching();
        }
    }

    /** Reschedules offline pages (using appropriate version of Background Task Scheduler). */
    protected void rescheduleOfflinePages() {
        BackgroundScheduler.getInstance().reschedule();
    }

    @Override
    public void onInitializeTasks() {
        rescheduleBackgroundSyncTasksOnUpgrade();
        reschedulePrecacheTasksOnUpgrade();
        rescheduleSnippetsTasksOnUpgrade();
    }
}

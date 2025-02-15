// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.suggestions;

import org.chromium.base.ContextUtils;
import org.chromium.base.annotations.JNIAdditionalImport;
import org.chromium.chrome.browser.ChromeFeatureList;
import org.chromium.chrome.browser.ntp.NewTabPage;
import org.chromium.chrome.browser.partnercustomizations.HomepageManager;
import org.chromium.chrome.browser.profiles.Profile;

/**
 * Methods to bridge into native history to provide most recent urls, titles and thumbnails.
 */
@JNIAdditionalImport(MostVisitedSites.class) // Needed for the Observer usage in the native calls.
public class MostVisitedSitesBridge
        implements MostVisitedSites, HomepageManager.HomepageStateListener {
    private long mNativeMostVisitedSitesBridge;

    /**
     * MostVisitedSites constructor requires a valid user profile object.
     *
     * @param profile The profile for which to fetch most visited sites.
     */
    public MostVisitedSitesBridge(Profile profile) {
        mNativeMostVisitedSitesBridge = nativeInit(profile);
        // The first tile replaces the home page button (only) in Chrome Home. To support that,
        // provide information about the home page.
        if (ChromeFeatureList.isEnabled(ChromeFeatureList.CHROME_HOME)) {
            nativeSetHomePageClient(mNativeMostVisitedSitesBridge, new HomePageClient() {
                @Override
                public boolean isHomePageEnabled() {
                    return HomepageManager.isHomepageEnabled(ContextUtils.getApplicationContext());
                }

                @Override
                public boolean isNewTabPageUsedAsHomePage() {
                    return NewTabPage.isNTPUrl(getHomePageUrl());
                }

                @Override
                public String getHomePageUrl() {
                    return HomepageManager.getHomepageUri(ContextUtils.getApplicationContext());
                }
            });
            HomepageManager.getInstance(ContextUtils.getApplicationContext()).addListener(this);
        }
    }

    /**
     * Cleans up the C++ side of this class. This instance must not be used after calling destroy().
     */
    @Override
    public void destroy() {
        // Stop listening even if it was not started in the first place. (Handled without errors.)
        HomepageManager.getInstance(ContextUtils.getApplicationContext()).removeListener(this);
        assert mNativeMostVisitedSitesBridge != 0;
        nativeDestroy(mNativeMostVisitedSitesBridge);
        mNativeMostVisitedSitesBridge = 0;
    }

    @Override
    public void setObserver(final Observer observer, int numSites) {
        Observer wrappedObserver = new Observer() {
            @Override
            public void onMostVisitedURLsAvailable(
                    String[] titles, String[] urls, String[] whitelistIconPaths, int[] sources) {
                // Don't notify observer if we've already been destroyed.
                if (mNativeMostVisitedSitesBridge != 0) {
                    observer.onMostVisitedURLsAvailable(titles, urls, whitelistIconPaths, sources);
                }
            }
            @Override
            public void onIconMadeAvailable(String siteUrl) {
                // Don't notify observer if we've already been destroyed.
                if (mNativeMostVisitedSitesBridge != 0) {
                    observer.onIconMadeAvailable(siteUrl);
                }
            }
        };
        nativeSetObserver(mNativeMostVisitedSitesBridge, wrappedObserver, numSites);
    }

    @Override
    public void addBlacklistedUrl(String url) {
        nativeAddOrRemoveBlacklistedUrl(mNativeMostVisitedSitesBridge, url, true);
    }

    @Override
    public void removeBlacklistedUrl(String url) {
        nativeAddOrRemoveBlacklistedUrl(mNativeMostVisitedSitesBridge, url, false);
    }

    @Override
    public void recordPageImpression(int[] tileTypes, int[] sources, String[] tileUrls) {
        nativeRecordPageImpression(mNativeMostVisitedSitesBridge, tileTypes, sources, tileUrls);
    }

    @Override
    public void recordOpenedMostVisitedItem(
            int index, @TileVisualType int type, @TileSource int source) {
        nativeRecordOpenedMostVisitedItem(mNativeMostVisitedSitesBridge, index, type, source);
    }

    @Override
    public void onHomepageStateUpdated() {
        assert mNativeMostVisitedSitesBridge != 0;
        // Ensure even a blacklisted home page can be set as tile when (re-)enabling it.
        if (HomepageManager.isHomepageEnabled(ContextUtils.getApplicationContext())) {
            removeBlacklistedUrl(
                    HomepageManager.getHomepageUri(ContextUtils.getApplicationContext()));
        }
    }

    private native long nativeInit(Profile profile);
    private native void nativeDestroy(long nativeMostVisitedSitesBridge);
    private native void nativeSetObserver(
            long nativeMostVisitedSitesBridge, MostVisitedSites.Observer observer, int numSites);
    private native void nativeSetHomePageClient(
            long nativeMostVisitedSitesBridge, MostVisitedSites.HomePageClient homePageClient);
    private native void nativeAddOrRemoveBlacklistedUrl(
            long nativeMostVisitedSitesBridge, String url, boolean addUrl);
    private native void nativeRecordPageImpression(
            long nativeMostVisitedSitesBridge, int[] tileTypes, int[] sources, String[] tileUrls);
    private native void nativeRecordOpenedMostVisitedItem(
            long nativeMostVisitedSitesBridge, int index, int tileType, int source);
}

// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.share;

import android.app.Activity;
import android.content.Intent;
import android.support.test.filters.SmallTest;
import android.support.test.rule.UiThreadTestRule;

import org.junit.Assert;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseJUnit4ClassRunner;
import org.chromium.components.dom_distiller.core.DomDistillerUrlUtils;
import org.chromium.content.browser.test.NativeLibraryTestRule;

/**
 * Tests sharing URLs in reader mode (DOM distiller)
 */
@RunWith(BaseJUnit4ClassRunner.class)
public class ShareUrlTest {
    @Rule
    public NativeLibraryTestRule mActivityTestRule = new NativeLibraryTestRule();

    @Rule
    public UiThreadTestRule mUiThreadTestRule = new UiThreadTestRule();

    private static final String HTTP_URL = "http://www.google.com/";
    private static final String HTTPS_URL = "https://www.google.com/";

    @Before
    public void setUp() throws Exception {
        mActivityTestRule.loadNativeLibraryAndInitBrowserProcess();
    }

    private void assertCorrectUrl(final String originalUrl, final String sharedUrl)
            throws Throwable {
        mUiThreadTestRule.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                ShareParams params =
                        new ShareParams.Builder(new Activity(), "", sharedUrl).setText("").build();
                Intent intent = ShareHelper.getShareLinkIntent(params);
                Assert.assertTrue(intent.hasExtra(Intent.EXTRA_TEXT));
                String url = intent.getStringExtra(Intent.EXTRA_TEXT);
                Assert.assertEquals(originalUrl, url);
            }
        });
    }

    @Test
    @SmallTest
    public void testNormalUrl() throws Throwable {
        assertCorrectUrl(HTTP_URL, HTTP_URL);
        assertCorrectUrl(HTTPS_URL, HTTPS_URL);
    }

    @Test
    @SmallTest
    public void testDistilledUrl() throws Throwable {
        final String DomDistillerScheme = "chrome-distiller";
        String distilledHttpUrl =
                DomDistillerUrlUtils.getDistillerViewUrlFromUrl(DomDistillerScheme, HTTP_URL);
        String distilledHttpsUrl =
                DomDistillerUrlUtils.getDistillerViewUrlFromUrl(DomDistillerScheme, HTTPS_URL);

        assertCorrectUrl(HTTP_URL, distilledHttpUrl);
        assertCorrectUrl(HTTPS_URL, distilledHttpsUrl);
    }
}

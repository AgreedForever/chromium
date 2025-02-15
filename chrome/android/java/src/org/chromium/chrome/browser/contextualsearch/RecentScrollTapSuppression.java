// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.contextualsearch;

/**
 * Heuristic for Tap suppression after a recent scroll action.
 * Handles logging of results seen and activation.
 */
public class RecentScrollTapSuppression extends ContextualSearchHeuristic {
    private static final int DEFAULT_RECENT_SCROLL_SUPPRESSION_DURATION_MS = 300;

    private final int mDurationSinceRecentScrollMs;
    private final boolean mIsConditionSatisfied;

    /**
     * Constructs a Tap suppression heuristic that handles a Tap after a recent scroll.
     * This logs activation data that includes whether it activated for a threshold specified
     * by an experiment. This also logs Results-seen data to profile when results are seen relative
     * to a recent scroll.
     * @param selectionController The {@link ContextualSearchSelectionController}.
     */
    RecentScrollTapSuppression(ContextualSearchSelectionController selectionController) {
        long recentScrollTimeNs = selectionController.getLastScrollTime();
        if (recentScrollTimeNs > 0) {
            mDurationSinceRecentScrollMs =
                    (int) ((System.nanoTime() - recentScrollTimeNs) / NANOSECONDS_IN_A_MILLISECOND);
        } else {
            mDurationSinceRecentScrollMs = 0;
        }

        mIsConditionSatisfied = mDurationSinceRecentScrollMs > 0
                && mDurationSinceRecentScrollMs < DEFAULT_RECENT_SCROLL_SUPPRESSION_DURATION_MS;
    }

    @Override
    protected boolean isConditionSatisfiedAndEnabled() {
        return mIsConditionSatisfied;
    }

    @Override
    protected void logConditionState() {
        ContextualSearchUma.logRecentScrollSuppression(mIsConditionSatisfied);
    }

    @Override
    protected void logRankerTapSuppression(ContextualSearchRankerLogger logger) {
        logger.logFeature(ContextualSearchRankerLogger.Feature.DURATION_AFTER_SCROLL_MS,
                mDurationSinceRecentScrollMs);
    }
}

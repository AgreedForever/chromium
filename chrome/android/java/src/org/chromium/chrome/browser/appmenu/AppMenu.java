// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.appmenu;

import android.animation.Animator;
import android.animation.Animator.AnimatorListener;
import android.animation.AnimatorSet;
import android.annotation.SuppressLint;
import android.content.Context;
import android.content.res.Resources;
import android.graphics.Rect;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.LayerDrawable;
import android.support.annotation.IdRes;
import android.support.annotation.Nullable;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.LayoutInflater;
import android.view.Menu;
import android.view.MenuItem;
import android.view.Surface;
import android.view.View;
import android.view.View.MeasureSpec;
import android.view.View.OnKeyListener;
import android.view.ViewGroup;
import android.view.ViewStub;
import android.widget.AdapterView;
import android.widget.AdapterView.OnItemClickListener;
import android.widget.ImageButton;
import android.widget.ListView;
import android.widget.PopupWindow;
import android.widget.PopupWindow.OnDismissListener;

import org.chromium.base.AnimationFrameTimeHistogram;
import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.ContextUtils;
import org.chromium.base.SysUtils;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.omaha.UpdateMenuItemHelper;
import org.chromium.chrome.browser.util.AccessibilityUtil;
import org.chromium.chrome.browser.widget.PulseDrawable;

import java.util.ArrayList;
import java.util.List;

/**
 * Shows a popup of menuitems anchored to a host view. When a item is selected we call
 * Activity.onOptionsItemSelected with the appropriate MenuItem.
 *   - Only visible MenuItems are shown.
 *   - Disabled items are grayed out.
 */
public class AppMenu implements OnItemClickListener, OnKeyListener {

    private static final float LAST_ITEM_SHOW_FRACTION = 0.5f;

    private final Menu mMenu;
    private final int mItemRowHeight;
    private final int mItemDividerHeight;
    private final int mVerticalFadeDistance;
    private final int mNegativeSoftwareVerticalOffset;
    private final int[] mTempLocation;

    private PopupWindow mPopup;
    private ListView mListView;
    private AppMenuAdapter mAdapter;
    private AppMenuHandler mHandler;
    private View mFooterView;
    private int mCurrentScreenRotation = -1;
    private boolean mIsByPermanentButton;
    private AnimatorSet mMenuItemEnterAnimator;
    private AnimatorListener mAnimationHistogramRecorder = AnimationFrameTimeHistogram
            .getAnimatorRecorder("WrenchMenu.OpeningAnimationFrameTimes");

    /**
     * Creates and sets up the App Menu.
     * @param menu Original menu created by the framework.
     * @param itemRowHeight Desired height for each app menu row.
     * @param itemDividerHeight Desired height for the divider between app menu items.
     * @param handler AppMenuHandler receives callbacks from AppMenu.
     * @param res Resources object used to get dimensions and style attributes.
     */
    AppMenu(Menu menu, int itemRowHeight, int itemDividerHeight, AppMenuHandler handler,
            Resources res) {
        mMenu = menu;

        mItemRowHeight = itemRowHeight;
        assert mItemRowHeight > 0;

        mHandler = handler;

        mItemDividerHeight = itemDividerHeight;
        assert mItemDividerHeight >= 0;

        mNegativeSoftwareVerticalOffset =
                res.getDimensionPixelSize(R.dimen.menu_negative_software_vertical_offset);
        mVerticalFadeDistance = res.getDimensionPixelSize(R.dimen.menu_vertical_fade_distance);

        mTempLocation = new int[2];
    }

    /**
     * Notifies the menu that the contents of the menu item specified by {@code menuRowId} have
     * changed.  This should be called if icons, titles, etc. are changing for a particular menu
     * item while the menu is open.
     * @param menuRowId The id of the menu item to change.  This must be a row id and not a child
     *                  id.
     */
    public void menuItemContentChanged(int menuRowId) {
        // Make sure we have all the valid state objects we need.
        if (mAdapter == null || mMenu == null || mPopup == null || mListView == null) {
            return;
        }

        // Calculate the item index.
        int index = -1;
        int menuSize = mMenu.size();
        for (int i = 0; i < menuSize; i++) {
            if (mMenu.getItem(i).getItemId() == menuRowId) {
                index = i;
                break;
            }
        }
        if (index == -1) return;

        // Check if the item is visible.
        int startIndex = mListView.getFirstVisiblePosition();
        int endIndex = mListView.getLastVisiblePosition();
        if (index < startIndex || index > endIndex) return;

        // Grab the correct View.
        View view = mListView.getChildAt(index - startIndex);
        if (view == null) return;

        // Cause the Adapter to re-populate the View.
        mListView.getAdapter().getView(index, view, mListView);
    }

    /**
     * Creates and shows the app menu anchored to the specified view.
     *
     * @param context             The context of the AppMenu (ensure the proper theme is set on this
     * context).
     * @param anchorView          The anchor {@link View} of the {@link ListPopupWindow}.
     * @param isByPermanentButton Whether or not permanent hardware button triggered it. (oppose to
     *                            software button or keyboard).
     * @param screenRotation      Current device screen rotation.
     * @param visibleDisplayFrame The display area rect in which AppMenu is supposed to fit in.
     * @param screenHeight        Current device screen height.
     * @param footerResourceId    The resource id for a view to add to the end of the menu list. Can
     *                            be 0 if no such view is required.
     * @param highlightedItemId   The resource id of the menu item that should be highlighted.  Can
     *                            be {@code null} if no item should be highlighted.  Note that
     *                            {@code 0} is dedicated to custom menu items and can be declared by
     *                            external apps.
     */
    @SuppressLint("ResourceType")
    void show(Context context, final View anchorView, boolean isByPermanentButton,
            int screenRotation, Rect visibleDisplayFrame, int screenHeight,
            @IdRes int footerResourceId, Integer highlightedItemId) {
        mPopup = new PopupWindow(context);
        mPopup.setFocusable(true);
        mPopup.setInputMethodMode(PopupWindow.INPUT_METHOD_NOT_NEEDED);

        boolean anchorAtBottom = isAnchorAtBottom(anchorView, visibleDisplayFrame);
        int footerHeight = 0;
        mPopup.setOnDismissListener(new OnDismissListener() {
            @Override
            public void onDismiss() {
                if (anchorView instanceof ImageButton) {
                    ((ImageButton) anchorView).setSelected(false);
                }

                if (mMenuItemEnterAnimator != null) mMenuItemEnterAnimator.cancel();

                mHandler.appMenuDismissed();
                mHandler.onMenuVisibilityChanged(false);
            }
        });

        // Some OEMs don't actually let us change the background... but they still return the
        // padding of the new background, which breaks the menu height.  If we still have a
        // drawable here even though our style says @null we should use this padding instead...
        Drawable originalBgDrawable = mPopup.getBackground();

        // Need to explicitly set the background here.  Relying on it being set in the style caused
        // an incorrectly drawn background.
        if (isByPermanentButton) {
            mPopup.setBackgroundDrawable(
                    ApiCompatibilityUtils.getDrawable(context.getResources(), R.drawable.menu_bg));
        } else {
            mPopup.setBackgroundDrawable(ApiCompatibilityUtils.getDrawable(
                    context.getResources(), R.drawable.edge_menu_bg));
            mPopup.setAnimationStyle(
                    anchorAtBottom ? R.style.OverflowMenuAnimBottom : R.style.OverflowMenuAnim);
        }

        // Turn off window animations for low end devices.
        if (SysUtils.isLowEndDevice()) mPopup.setAnimationStyle(0);

        Rect bgPadding = new Rect();
        mPopup.getBackground().getPadding(bgPadding);

        int menuWidth = context.getResources().getDimensionPixelSize(R.dimen.menu_width);
        int popupWidth = menuWidth + bgPadding.left + bgPadding.right;

        mPopup.setWidth(popupWidth);

        mCurrentScreenRotation = screenRotation;
        mIsByPermanentButton = isByPermanentButton;

        // Extract visible items from the Menu.
        int numItems = mMenu.size();
        List<MenuItem> menuItems = new ArrayList<MenuItem>();
        for (int i = 0; i < numItems; ++i) {
            MenuItem item = mMenu.getItem(i);
            if (item.isVisible()) {
                menuItems.add(item);
            }
        }

        Rect sizingPadding = new Rect(bgPadding);
        if (isByPermanentButton && originalBgDrawable != null) {
            Rect originalPadding = new Rect();
            originalBgDrawable.getPadding(originalPadding);
            sizingPadding.top = originalPadding.top;
            sizingPadding.bottom = originalPadding.bottom;
        }

        // A List adapter for visible items in the Menu. The first row is added as a header to the
        // list view.
        mAdapter = new AppMenuAdapter(
                this, menuItems, LayoutInflater.from(context), highlightedItemId);

        ViewGroup contentView =
                (ViewGroup) LayoutInflater.from(context).inflate(R.layout.app_menu_layout, null);
        mListView = (ListView) contentView.findViewById(R.id.app_menu_list);
        mListView.setAdapter(mAdapter);

        if (footerResourceId != 0) {
            // TODO(crbug.com/635567): Fix lint error properly.
            ViewStub footerStub = (ViewStub) contentView.findViewById(R.id.app_menu_footer_stub);
            footerStub.setLayoutResource(footerResourceId);
            mFooterView = footerStub.inflate();
            int heightMeasureSpec = MeasureSpec.makeMeasureSpec(0, MeasureSpec.UNSPECIFIED);
            int widthMeasureSpec = MeasureSpec.makeMeasureSpec(menuWidth, MeasureSpec.EXACTLY);
            mFooterView.measure(widthMeasureSpec, heightMeasureSpec);
            footerHeight = mFooterView.getMeasuredHeight();
            highlightViewInFooter(highlightedItemId, mFooterView);
        } else {
            mFooterView = null;
        }

        int popupHeight = setMenuHeight(menuItems.size(), visibleDisplayFrame, screenHeight,
                sizingPadding, footerHeight, anchorView);
        int[] popupPosition = getPopupPosition(mCurrentScreenRotation, visibleDisplayFrame,
                sizingPadding, anchorView, anchorAtBottom, popupWidth, popupHeight);

        mPopup.setContentView(contentView);
        mPopup.showAtLocation(
                anchorView.getRootView(), Gravity.NO_GRAVITY, popupPosition[0], popupPosition[1]);

        mListView.setOnItemClickListener(this);
        mListView.setItemsCanFocus(true);
        mListView.setOnKeyListener(this);

        mHandler.onMenuVisibilityChanged(true);

        if (mVerticalFadeDistance > 0) {
            mListView.setVerticalFadingEdgeEnabled(true);
            mListView.setFadingEdgeLength(mVerticalFadeDistance);
        }

        // Don't animate the menu items for low end devices.
        if (!SysUtils.isLowEndDevice()) {
            mListView.addOnLayoutChangeListener(new View.OnLayoutChangeListener() {
                @Override
                public void onLayoutChange(View v, int left, int top, int right, int bottom,
                        int oldLeft, int oldTop, int oldRight, int oldBottom) {
                    mListView.removeOnLayoutChangeListener(this);
                    runMenuItemEnterAnimations();
                }
            });
        }
    }

    /**
     * Highlights the given {@code footerView} or one of its child. If {@code highlightedItemId} is
     * same as the id of the {@code footerView}, the entire {@code footerView} will be highlighted.
     * Otherwise it will only use a circle pulse around the individual child view.
     * @param highlightedItemId The resource id of the view that should be highlighted. Can be
     *                          {@code null} if no item should be highlighted.
     * @param footerView        The root view in which the {@code highlightedItemId} is to be found.
     */
    private void highlightViewInFooter(Integer highlightedItemId, View footerView) {
        if (highlightedItemId == null) return;

        View view = footerView.findViewById(highlightedItemId);
        if (view == null) return;

        PulseDrawable pulse = view == footerView
                ? PulseDrawable.createHighlight()
                : PulseDrawable.createCircle(footerView.getContext());

        Drawable newBackground = pulse;
        Drawable currentBackground = view.getBackground();
        if (currentBackground != null && currentBackground.getConstantState() != null) {
            Drawable backgroundClone =
                    currentBackground.getConstantState().newDrawable(footerView.getResources());
            newBackground = new LayerDrawable(new Drawable[] {backgroundClone, pulse});
        }

        view.setBackground(newBackground);
        pulse.start();
    }

    /**
     * @return The footer view for the menu or null if one has not been set.
     */
    @Nullable
    public View getFooterView() {
        return mFooterView;
    }

    private boolean isAnchorAtBottom(View anchorView, Rect visibleDisplayFrame) {
        anchorView.getLocationOnScreen(mTempLocation);
        return (mTempLocation[1] + anchorView.getHeight()) >= visibleDisplayFrame.bottom;
    }

    private int[] getPopupPosition(int screenRotation, Rect appRect, Rect padding, View anchorView,
            boolean anchorAtBottom, int popupWidth, int popupHeight) {
        anchorView.getLocationInWindow(mTempLocation);
        int anchorViewX = mTempLocation[0];
        int anchorViewY = mTempLocation[1];

        int[] offsets = new int[2];
        // If we have a hardware menu button, locate the app menu closer to the estimated
        // hardware menu button location.
        if (mIsByPermanentButton) {
            int horizontalOffset = -anchorViewX;
            switch (screenRotation) {
                case Surface.ROTATION_0:
                case Surface.ROTATION_180:
                    horizontalOffset += (appRect.width() - popupWidth) / 2;
                    break;
                case Surface.ROTATION_90:
                    horizontalOffset += appRect.width() - popupWidth;
                    break;
                case Surface.ROTATION_270:
                    break;
                default:
                    assert false;
                    break;
            }
            offsets[0] = horizontalOffset;
            // The menu is displayed above the anchored view, so shift the menu up by the bottom
            // padding of the background.
            offsets[1] = -padding.bottom;
        } else {
            offsets[1] = -mNegativeSoftwareVerticalOffset;

            // If the anchor is at the bottom of the screen, align the popup with the bottom of the
            // anchor. The anchor may not be fully visible, so
            // (appRect.bottom - anchorViewLocationOnScreenY) is used to determine the visible
            // bottom edge of the anchor view.
            if (anchorAtBottom) {
                anchorView.getLocationOnScreen(mTempLocation);
                int anchorViewLocationOnScreenY = mTempLocation[1];
                offsets[1] += appRect.bottom - anchorViewLocationOnScreenY - popupHeight;
            }

            if (!ApiCompatibilityUtils.isLayoutRtl(anchorView.getRootView())) {
                offsets[0] = anchorView.getWidth() - popupWidth;
            }
        }

        int xPos = anchorViewX + offsets[0];
        int yPos = anchorViewY + offsets[1];
        int[] position = {xPos, yPos};
        return position;
    }

    /**
     * Handles clicks on the AppMenu popup.
     * @param menuItem The menu item in the popup that was clicked.
     */
    void onItemClick(MenuItem menuItem) {
        if (menuItem.isEnabled()) {
            if (menuItem.getItemId() == R.id.update_menu_id) {
                UpdateMenuItemHelper.getInstance().setMenuItemClicked();
            }
            dismiss();
            mHandler.onOptionsItemSelected(menuItem);
        }
    }

    /**
     * Handles long clicks on image buttons on the AppMenu popup.
     * @param menuItem The menu item in the popup that was long clicked.
     * @param view The anchor view of the menu item.
     */
    boolean onItemLongClick(MenuItem menuItem, View view) {
        if (!menuItem.isEnabled()) return false;

        String description = null;
        Context context = ContextUtils.getApplicationContext();
        Resources resources = context.getResources();
        final int itemId = menuItem.getItemId();

        if (itemId == R.id.forward_menu_id) {
            description = resources.getString(R.string.menu_forward);
        } else if (itemId == R.id.bookmark_this_page_id) {
            description = resources.getString(R.string.menu_bookmark);
        } else if (itemId == R.id.offline_page_id) {
            description = resources.getString(R.string.menu_download);
        } else if (itemId == R.id.info_menu_id) {
            description = resources.getString(R.string.menu_page_info);
        } else if (itemId == R.id.reload_menu_id) {
            description = (menuItem.getIcon().getLevel()
                                  == resources.getInteger(R.integer.reload_button_level_reload))
                    ? resources.getString(R.string.menu_refresh)
                    : resources.getString(R.string.menu_stop_refresh);
        }
        return AccessibilityUtil.showAccessibilityToast(context, view, description);
    }

    @Override
    public void onItemClick(AdapterView<?> parent, View view, int position, long id) {
        onItemClick(mAdapter.getItem(position));
    }

    @Override
    public boolean onKey(View v, int keyCode, KeyEvent event) {
        if (mListView == null) return false;

        if (event.getKeyCode() == KeyEvent.KEYCODE_MENU) {
            if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0) {
                event.startTracking();
                v.getKeyDispatcherState().startTracking(event, this);
                return true;
            } else if (event.getAction() == KeyEvent.ACTION_UP) {
                v.getKeyDispatcherState().handleUpEvent(event);
                if (event.isTracking() && !event.isCanceled()) {
                    dismiss();
                    return true;
                }
            }
        }
        return false;
    }

    /**
     * Dismisses the app menu and cancels the drag-to-scroll if it is taking place.
     */
    void dismiss() {
        if (isShowing()) {
            mPopup.dismiss();
        }
    }

    /**
     * @return Whether the app menu is currently showing.
     */
    boolean isShowing() {
        if (mPopup == null) {
            return false;
        }
        return mPopup.isShowing();
    }

    /**
     * @return {@link PopupWindow} that displays all the menu options and optional footer.
     */
    PopupWindow getPopup() {
        return mPopup;
    }

    /**
     * @return {@link ListView} that contains all of the menu options.
     */
    ListView getListView() {
        return mListView;
    }

    /**
     * @return The menu instance inside of this class.
     */
    public Menu getMenu() {
        return mMenu;
    }

    private int setMenuHeight(int numMenuItems, Rect appDimensions, int screenHeight, Rect padding,
            int footerHeight, View anchorView) {
        int menuHeight;
        anchorView.getLocationOnScreen(mTempLocation);
        int anchorViewY = mTempLocation[1] - appDimensions.top;

        if (isAnchorAtBottom(anchorView, appDimensions)) anchorViewY += anchorView.getHeight();

        int anchorViewImpactHeight = mIsByPermanentButton ? anchorView.getHeight() : 0;

        // Set appDimensions.height() for abnormal anchorViewLocation.
        if (anchorViewY > screenHeight) {
            anchorViewY = appDimensions.height();
        }
        int availableScreenSpace = Math.max(
                anchorViewY, appDimensions.height() - anchorViewY - anchorViewImpactHeight);

        availableScreenSpace -= padding.bottom + footerHeight;
        if (mIsByPermanentButton) availableScreenSpace -= padding.top;

        int numCanFit = availableScreenSpace / (mItemRowHeight + mItemDividerHeight);

        // Fade out the last item if we cannot fit all items.
        if (numCanFit < numMenuItems) {
            int spaceForFullItems = numCanFit * (mItemRowHeight + mItemDividerHeight);
            spaceForFullItems += footerHeight;

            int spaceForPartialItem = (int) (LAST_ITEM_SHOW_FRACTION * mItemRowHeight);
            // Determine which item needs hiding.
            if (spaceForFullItems + spaceForPartialItem < availableScreenSpace) {
                menuHeight = spaceForFullItems + spaceForPartialItem + padding.top + padding.bottom;
            } else {
                menuHeight = spaceForFullItems - mItemRowHeight + spaceForPartialItem + padding.top
                        + padding.bottom;
            }
        } else {
            int spaceForFullItems = numMenuItems * (mItemRowHeight + mItemDividerHeight);
            spaceForFullItems += footerHeight;
            menuHeight = spaceForFullItems + padding.top + padding.bottom;
        }
        mPopup.setHeight(menuHeight);
        return menuHeight;
    }

    private void runMenuItemEnterAnimations() {
        mMenuItemEnterAnimator = new AnimatorSet();
        AnimatorSet.Builder builder = null;

        ViewGroup list = mListView;
        for (int i = 0; i < list.getChildCount(); i++) {
            View view = list.getChildAt(i);
            Object animatorObject = view.getTag(R.id.menu_item_enter_anim_id);
            if (animatorObject != null) {
                if (builder == null) {
                    builder = mMenuItemEnterAnimator.play((Animator) animatorObject);
                } else {
                    builder.with((Animator) animatorObject);
                }
            }
        }

        mMenuItemEnterAnimator.addListener(mAnimationHistogramRecorder);
        mMenuItemEnterAnimator.start();
    }
}

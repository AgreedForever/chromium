// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.signin;

import android.accounts.Account;
import android.annotation.TargetApi;
import android.app.Activity;
import android.app.Dialog;
import android.app.DialogFragment;
import android.app.ProgressDialog;
import android.content.Context;
import android.content.Intent;
import android.content.res.Resources;
import android.graphics.Bitmap;
import android.graphics.Bitmap.Config;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.PorterDuff;
import android.graphics.PorterDuffXfermode;
import android.graphics.Rect;
import android.graphics.drawable.BitmapDrawable;
import android.graphics.drawable.Drawable;
import android.os.Build;
import android.os.Bundle;
import android.os.UserManager;
import android.preference.Preference;
import android.preference.Preference.OnPreferenceClickListener;
import android.preference.PreferenceCategory;
import android.preference.PreferenceFragment;
import android.preference.PreferenceScreen;
import android.support.annotation.Nullable;
import android.support.v7.content.res.AppCompatResources;
import android.text.TextUtils;
import android.util.Pair;
import android.widget.ListView;

import org.chromium.base.ApiCompatibilityUtils;
import org.chromium.base.ContextUtils;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.AppHooks;
import org.chromium.chrome.browser.preferences.ChromeBasePreference;
import org.chromium.chrome.browser.preferences.ManagedPreferenceDelegate;
import org.chromium.chrome.browser.preferences.PrefServiceBridge;
import org.chromium.chrome.browser.preferences.Preferences;
import org.chromium.chrome.browser.preferences.PreferencesLauncher;
import org.chromium.chrome.browser.preferences.SyncPreference;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.profiles.ProfileAccountManagementMetrics;
import org.chromium.chrome.browser.profiles.ProfileDownloader;
import org.chromium.chrome.browser.signin.SignOutDialogFragment.SignOutDialogListener;
import org.chromium.chrome.browser.signin.SigninManager.SignInStateObserver;
import org.chromium.chrome.browser.sync.ProfileSyncService;
import org.chromium.chrome.browser.sync.ProfileSyncService.SyncStateChangedListener;
import org.chromium.chrome.browser.sync.ui.SyncCustomizationFragment;
import org.chromium.chrome.browser.util.IntentUtils;
import org.chromium.components.signin.AccountManagerHelper;
import org.chromium.components.signin.ChromeSigninController;

import java.util.HashMap;

/**
 * The settings screen with information and settings related to the user's accounts.
 *
 * This shows which accounts the user is signed in with, allows the user to sign out of Chrome,
 * links to sync settings, has links to add accounts and go incognito, and shows parental settings
 * if a child account is in use.
 *
 * Note: This can be triggered from a web page, e.g. a GAIA sign-in page.
 */
public class AccountManagementFragment extends PreferenceFragment
        implements SignOutDialogListener, ProfileDownloader.Observer,
                SyncStateChangedListener, SignInStateObserver,
                ConfirmManagedSyncDataDialog.Listener {
    private static final String TAG = "AcctManagementPref";

    public static final String SIGN_OUT_DIALOG_TAG = "sign_out_dialog_tag";
    private static final String CLEAR_DATA_PROGRESS_DIALOG_TAG = "clear_data_progress";

    /**
     * The key for an integer value in
     * {@link Preferences#EXTRA_SHOW_FRAGMENT_ARGUMENTS} bundle to
     * specify the correct GAIA service that has triggered the dialog.
     * If the argument is not set, GAIA_SERVICE_TYPE_NONE is used as the origin of the dialog.
     */
    public static final String SHOW_GAIA_SERVICE_TYPE_EXTRA = "ShowGAIAServiceType";

    /**
     * SharedPreference name for the preference that disables signing out of Chrome.
     * Signing out is forever disabled once Chrome signs the user in automatically
     * if the device has a child account or if the device is an Android EDU device.
     */
    private static final String SIGN_OUT_ALLOWED = "auto_signed_in_school_account";

    private static final HashMap<String, Pair<String, Drawable>> sToNamePicture = new HashMap<>();

    private static String sChildAccountId;
    private static Drawable sCachedBadgedPicture;

    public static final String PREF_ACCOUNTS_CATEGORY = "accounts_category";
    public static final String PREF_PARENTAL_SETTINGS = "parental_settings";
    public static final String PREF_PARENT_ACCOUNTS = "parent_accounts";
    public static final String PREF_CHILD_CONTENT = "child_content";
    public static final String PREF_CHILD_CONTENT_DIVIDER = "child_content_divider";
    public static final String PREF_GOOGLE_ACTIVITY_CONTROLS = "google_activity_controls";
    public static final String PREF_SYNC_SETTINGS = "sync_settings";
    public static final String PREF_SIGN_OUT = "sign_out";
    public static final String PREF_SIGN_OUT_DIVIDER = "sign_out_divider";

    private static final String ACCOUNT_SETTINGS_ACTION = "android.settings.ACCOUNT_SYNC_SETTINGS";
    private static final String ACCOUNT_SETTINGS_ACCOUNT_KEY = "account";

    private int mGaiaServiceType;

    private Profile mProfile;
    private String mSignedInAccountName;

    @Override
    public void onCreate(Bundle savedState) {
        super.onCreate(savedState);

        // Prevent sync from starting if it hasn't already to give the user a chance to change
        // their sync settings.
        ProfileSyncService syncService = ProfileSyncService.get();
        if (syncService != null) {
            syncService.setSetupInProgress(true);
        }

        mGaiaServiceType = AccountManagementScreenHelper.GAIA_SERVICE_TYPE_NONE;
        if (getArguments() != null) {
            mGaiaServiceType =
                    getArguments().getInt(SHOW_GAIA_SERVICE_TYPE_EXTRA, mGaiaServiceType);
        }

        mProfile = Profile.getLastUsedProfile();

        AccountManagementScreenHelper.logEvent(
                ProfileAccountManagementMetrics.VIEW,
                mGaiaServiceType);

        startFetchingAccountsInformation(getActivity(), mProfile);
    }

    @Override
    public void onActivityCreated(@Nullable Bundle savedInstanceState) {
        super.onActivityCreated(savedInstanceState);

        ListView list = (ListView) getView().findViewById(android.R.id.list);
        list.setDivider(null);
    }

    @Override
    public void onResume() {
        super.onResume();
        SigninManager.get(getActivity()).addSignInStateObserver(this);
        ProfileDownloader.addObserver(this);
        ProfileSyncService syncService = ProfileSyncService.get();
        if (syncService != null) {
            syncService.addSyncStateChangedListener(this);
        }

        update();
    }

    @Override
    public void onPause() {
        super.onPause();
        SigninManager.get(getActivity()).removeSignInStateObserver(this);
        ProfileDownloader.removeObserver(this);
        ProfileSyncService syncService = ProfileSyncService.get();
        if (syncService != null) {
            syncService.removeSyncStateChangedListener(this);
        }
    }

    @Override
    public void onDestroy() {
        super.onDestroy();

        // Allow sync to begin syncing if it hasn't yet.
        ProfileSyncService syncService = ProfileSyncService.get();
        if (syncService != null) {
            syncService.setSetupInProgress(false);
        }
    }

    /**
     * Initiate fetching the user accounts data (images and the full name).
     * Fetched data will be sent to observers of ProfileDownloader.
     *
     * @param context A context to get resources, current theme, etc.
     * @param profile Profile to use.
     */
    private static void startFetchingAccountsInformation(Context context, Profile profile) {
        Account[] accounts = AccountManagerHelper.get().getGoogleAccounts();
        for (Account account : accounts) {
            startFetchingAccountInformation(context, profile, account.name);
        }
    }

    public void update() {
        final Context context = getActivity();
        if (context == null) return;

        if (getPreferenceScreen() != null) getPreferenceScreen().removeAll();

        mSignedInAccountName = ChromeSigninController.get().getSignedInAccountName();
        if (mSignedInAccountName == null) {
            // The AccountManagementFragment can only be shown when the user is signed in. If the
            // user is signed out, exit the fragment.
            getActivity().finish();
            return;
        }

        addPreferencesFromResource(R.xml.account_management_preferences);

        String fullName = getCachedUserName(mSignedInAccountName);
        if (TextUtils.isEmpty(fullName)) {
            fullName = ProfileDownloader.getCachedFullName(mProfile);
        }
        if (TextUtils.isEmpty(fullName)) fullName = mSignedInAccountName;

        getActivity().setTitle(fullName);

        configureSignOutSwitch();
        configureChildAccountPreferences();
        configureSyncSettings();
        configureGoogleActivityControls();

        updateAccountsList();
    }

    @TargetApi(Build.VERSION_CODES.LOLLIPOP)
    private boolean canAddAccounts() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) return true;

        UserManager userManager = (UserManager) getActivity()
                .getSystemService(Context.USER_SERVICE);
        return !userManager.hasUserRestriction(UserManager.DISALLOW_MODIFY_ACCOUNTS);
    }

    private void configureSignOutSwitch() {
        Preference signOutSwitch = findPreference(PREF_SIGN_OUT);
        if (mProfile.isChild()) {
            getPreferenceScreen().removePreference(signOutSwitch);
            getPreferenceScreen().removePreference(findPreference(PREF_SIGN_OUT_DIVIDER));
        } else {
            signOutSwitch.setEnabled(getSignOutAllowedPreferenceValue());
            signOutSwitch.setOnPreferenceClickListener(new OnPreferenceClickListener() {
                @Override
                public boolean onPreferenceClick(Preference preference) {
                    if (!isVisible() || !isResumed()) return false;

                    if (mSignedInAccountName != null && getSignOutAllowedPreferenceValue()) {
                        AccountManagementScreenHelper.logEvent(
                                ProfileAccountManagementMetrics.TOGGLE_SIGNOUT,
                                mGaiaServiceType);

                        String managementDomain =
                                SigninManager.get(getActivity()).getManagementDomain();
                        if (managementDomain != null) {
                            // Show the 'You are signing out of a managed account' dialog.
                            ConfirmManagedSyncDataDialog.showSignOutFromManagedAccountDialog(
                                    AccountManagementFragment.this, getFragmentManager(),
                                    getResources(), managementDomain);
                        } else {
                            // Show the 'You are signing out' dialog.
                            SignOutDialogFragment signOutFragment = new SignOutDialogFragment();
                            Bundle args = new Bundle();
                            args.putInt(SHOW_GAIA_SERVICE_TYPE_EXTRA, mGaiaServiceType);
                            signOutFragment.setArguments(args);

                            signOutFragment.setTargetFragment(AccountManagementFragment.this, 0);
                            signOutFragment.show(getFragmentManager(), SIGN_OUT_DIALOG_TAG);
                        }

                        return true;
                    }

                    return false;
                }
            });
        }
    }

    private void configureSyncSettings() {
        final Preferences preferences = (Preferences) getActivity();
        findPreference(PREF_SYNC_SETTINGS)
                .setOnPreferenceClickListener(new OnPreferenceClickListener() {
                    @Override
                    public boolean onPreferenceClick(Preference preference) {
                        if (!isVisible() || !isResumed()) return false;

                        if (ProfileSyncService.get() == null) return true;

                        Bundle args = new Bundle();
                        args.putString(
                                SyncCustomizationFragment.ARGUMENT_ACCOUNT, mSignedInAccountName);
                        preferences.startFragment(SyncCustomizationFragment.class.getName(), args);

                        return true;
                    }
                });
    }

    private void configureGoogleActivityControls() {
        Preference pref = findPreference(PREF_GOOGLE_ACTIVITY_CONTROLS);
        if (mProfile.isChild()) {
            pref.setSummary(R.string.sign_in_google_activity_controls_message_child_account);
        }
        pref.setOnPreferenceClickListener(new OnPreferenceClickListener() {
            @Override
            public boolean onPreferenceClick(Preference preference) {
                Activity activity = getActivity();
                AppHooks.get().createGoogleActivityController().openWebAndAppActivitySettings(
                        activity, mSignedInAccountName);
                RecordUserAction.record("Signin_AccountSettings_GoogleActivityControlsClicked");
                return true;
            }
        });
    }

    private void configureChildAccountPreferences() {
        Preference parentAccounts = findPreference(PREF_PARENT_ACCOUNTS);
        Preference childContent = findPreference(PREF_CHILD_CONTENT);
        if (mProfile.isChild()) {
            Resources res = getActivity().getResources();
            PrefServiceBridge prefService = PrefServiceBridge.getInstance();

            String firstParent = prefService.getSupervisedUserCustodianEmail();
            String secondParent = prefService.getSupervisedUserSecondCustodianEmail();
            String parentText;

            if (!secondParent.isEmpty()) {
                parentText = res.getString(R.string.account_management_two_parent_names,
                        firstParent, secondParent);
            } else if (!firstParent.isEmpty()) {
                parentText = res.getString(R.string.account_management_one_parent_name,
                        firstParent);
            } else {
                parentText = res.getString(R.string.account_management_no_parental_data);
            }
            parentAccounts.setSummary(parentText);
            parentAccounts.setSelectable(false);

            final int childContentSummary;
            int defaultBehavior = prefService.getDefaultSupervisedUserFilteringBehavior();
            if (defaultBehavior == PrefServiceBridge.SUPERVISED_USER_FILTERING_BLOCK) {
                childContentSummary = R.string.account_management_child_content_approved;
            } else if (prefService.isSupervisedUserSafeSitesEnabled()) {
                childContentSummary = R.string.account_management_child_content_filter_mature;
            } else {
                childContentSummary = R.string.account_management_child_content_all;
            }
            childContent.setSummary(childContentSummary);
            childContent.setSelectable(false);

            Drawable newIcon = ApiCompatibilityUtils.getDrawable(
                    getResources(), R.drawable.ic_drive_site_white_24dp);
            newIcon.mutate().setColorFilter(
                    ApiCompatibilityUtils.getColor(getResources(), R.color.google_grey_600),
                    PorterDuff.Mode.SRC_IN);
            childContent.setIcon(newIcon);
        } else {
            PreferenceScreen prefScreen = getPreferenceScreen();
            prefScreen.removePreference(findPreference(PREF_PARENTAL_SETTINGS));
            prefScreen.removePreference(parentAccounts);
            prefScreen.removePreference(childContent);
            prefScreen.removePreference(findPreference(PREF_CHILD_CONTENT_DIVIDER));
        }
    }

    private void updateAccountsList() {
        PreferenceCategory accountsCategory =
                (PreferenceCategory) findPreference(PREF_ACCOUNTS_CATEGORY);
        if (accountsCategory == null) return;

        accountsCategory.removeAll();

        boolean isChildAccount = mProfile.isChild();
        Account[] accounts = AccountManagerHelper.get().getGoogleAccounts();
        for (final Account account : accounts) {
            Preference pref = new Preference(getActivity());
            pref.setLayoutResource(R.layout.account_management_account_row);
            pref.setTitle(account.name);
            pref.setIcon(isChildAccount ? getBadgedUserPicture(getActivity(), account.name)
                                        : getUserPicture(getActivity(), account.name));

            pref.setOnPreferenceClickListener(new OnPreferenceClickListener() {
                @Override
                public boolean onPreferenceClick(Preference preference) {
                    Intent intent = new Intent(ACCOUNT_SETTINGS_ACTION);
                    intent.putExtra(ACCOUNT_SETTINGS_ACCOUNT_KEY, account);
                    return IntentUtils.safeStartActivity(getActivity(), intent);
                }
            });

            accountsCategory.addPreference(pref);
        }

        if (!isChildAccount) {
            accountsCategory.addPreference(createAddAccountPreference());
        }
    }

    private ChromeBasePreference createAddAccountPreference() {
        ChromeBasePreference addAccountPreference = new ChromeBasePreference(getActivity());
        addAccountPreference.setLayoutResource(R.layout.account_management_account_row);
        addAccountPreference.setIcon(R.drawable.add_circle_blue);
        addAccountPreference.setTitle(R.string.account_management_add_account_title);
        addAccountPreference.setOnPreferenceClickListener(new OnPreferenceClickListener() {
            @Override
            public boolean onPreferenceClick(Preference preference) {
                if (!isVisible() || !isResumed()) return false;

                AccountManagementScreenHelper.logEvent(
                        ProfileAccountManagementMetrics.ADD_ACCOUNT, mGaiaServiceType);

                AccountAdder.getInstance().addAccount(
                        getActivity(), AccountAdder.ADD_ACCOUNT_RESULT);

                // Return to the last opened tab if triggered from the content area.
                if (mGaiaServiceType != AccountManagementScreenHelper.GAIA_SERVICE_TYPE_NONE) {
                    if (isAdded()) getActivity().finish();
                }

                return true;
            }
        });
        addAccountPreference.setManagedPreferenceDelegate(new ManagedPreferenceDelegate() {
            @Override
            public boolean isPreferenceControlledByPolicy(Preference preference) {
                return !canAddAccounts();
            }
        });
        return addAccountPreference;
    }

    // ProfileDownloader.Observer implementation:

    @Override
    public void onProfileDownloaded(String accountId, String fullName, String givenName,
            Bitmap bitmap) {
        updateUserNamePictureCache(getActivity(), accountId, fullName, bitmap);
        updateAccountsList();
    }

    // SignOutDialogListener implementation:

    /**
     * This class must be public and static. Otherwise an exception will be thrown when Android
     * recreates the fragment (e.g. after a configuration change).
     */
    public static class ClearDataProgressDialog extends DialogFragment {
        @Override
        public void onCreate(Bundle savedInstanceState) {
            super.onCreate(savedInstanceState);
            // Don't allow the dialog to be recreated by Android, since it wouldn't ever be
            // dismissed after recreation.
            if (savedInstanceState != null) dismiss();
        }

        @Override
        public Dialog onCreateDialog(Bundle savedInstanceState) {
            setCancelable(false);
            ProgressDialog dialog = new ProgressDialog(getActivity());
            dialog.setTitle(getString(R.string.wiping_profile_data_title));
            dialog.setMessage(getString(R.string.wiping_profile_data_message));
            dialog.setIndeterminate(true);
            return dialog;
        }
    }

    @Override
    public void onSignOutClicked() {
        // In case the user reached this fragment without being signed in, we guard the sign out so
        // we do not hit a native crash.
        if (!ChromeSigninController.get().isSignedIn()) return;

        final Activity activity = getActivity();
        final DialogFragment clearDataProgressDialog = new ClearDataProgressDialog();
        SigninManager.get(activity).signOut(null, new SigninManager.WipeDataHooks() {
            @Override
            public void preWipeData() {
                clearDataProgressDialog.show(
                        activity.getFragmentManager(), CLEAR_DATA_PROGRESS_DIALOG_TAG);
            }
            @Override
            public void postWipeData() {
                if (clearDataProgressDialog.isAdded()) {
                    clearDataProgressDialog.dismissAllowingStateLoss();
                }
            }
        });
        AccountManagementScreenHelper.logEvent(
                ProfileAccountManagementMetrics.SIGNOUT_SIGNOUT,
                mGaiaServiceType);
    }

    @Override
    public void onSignOutDialogDismissed(boolean signOutClicked) {
        if (!signOutClicked) {
            AccountManagementScreenHelper.logEvent(
                    ProfileAccountManagementMetrics.SIGNOUT_CANCEL,
                    mGaiaServiceType);
        }
    }

    // ConfirmManagedSyncDataDialog.Listener implementation
    @Override
    public void onConfirm() {
        onSignOutClicked();
    }

    @Override
    public void onCancel() {
        onSignOutDialogDismissed(false);
    }

    // ProfileSyncServiceListener implementation:

    @Override
    public void syncStateChanged() {
        SyncPreference pref = (SyncPreference) findPreference(PREF_SYNC_SETTINGS);
        if (pref != null) {
            pref.updateSyncSummaryAndIcon();
        }

        // TODO(crbug/557784): Show notification for sync error
    }

    // SignInStateObserver implementation:

    @Override
    public void onSignedIn() {
        update();
    }

    @Override
    public void onSignedOut() {
        update();
    }

    /**
     * Open the account management UI.
     * @param serviceType A signin::GAIAServiceType that triggered the dialog.
     */
    public static void openAccountManagementScreen(int serviceType) {
        Intent intent = PreferencesLauncher.createIntentForSettingsPage(
                ContextUtils.getApplicationContext(), AccountManagementFragment.class.getName());
        Bundle arguments = new Bundle();
        arguments.putInt(SHOW_GAIA_SERVICE_TYPE_EXTRA, serviceType);
        intent.putExtra(Preferences.EXTRA_SHOW_FRAGMENT_ARGUMENTS, arguments);
        ContextUtils.getApplicationContext().startActivity(intent);
    }

    /**
     * Converts a square user picture to a round user picture.
     *
     * @param context A context to get resources, current theme, etc.
     * @param bitmap A bitmap to convert.
     * @return A rounded picture bitmap.
     */
    public static Drawable makeRoundUserPicture(Context context, Bitmap bitmap) {
        if (bitmap == null) return null;

        int imageSizePx = context.getResources().getDimensionPixelSize(R.dimen.user_picture_size);
        Bitmap output = Bitmap.createBitmap(imageSizePx, imageSizePx, Config.ARGB_8888);
        Canvas canvas = new Canvas(output);

        final Paint paint = new Paint();
        canvas.drawARGB(0, 0, 0, 0);
        paint.setAntiAlias(true);
        paint.setColor(0xFFFFFFFF);
        canvas.drawCircle(imageSizePx * 0.5f, imageSizePx * 0.5f, imageSizePx * 0.5f, paint);
        paint.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.SRC_IN));

        Rect srcRect = new Rect(0, 0, bitmap.getWidth(), bitmap.getHeight());
        Rect dstRect = new Rect(0, 0, imageSizePx, imageSizePx);
        canvas.drawBitmap(bitmap, srcRect, dstRect, paint);
        return new BitmapDrawable(context.getResources(), output);
    }

    /**
     * Creates a new image with the picture overlaid by the badge.
     * @param userPicture A bitmap to overlay on.
     * @param badge A bitmap to overlay with.
     * @return A bitmap with the badge overlaying the {@code userPicture}.
     */
    private static Drawable overlayChildBadgeOnUserPicture(
            Drawable userPicture, Bitmap badge, Resources resources) {
        int imageSizePx = resources.getDimensionPixelOffset(R.dimen.user_picture_size);
        int borderSize = resources.getDimensionPixelOffset(R.dimen.badge_border_size);
        int badgeRadius = resources.getDimensionPixelOffset(R.dimen.badge_radius);

        // Create a larger image to accommodate the badge which spills the original picture.
        int badgedPictureWidth =
                resources.getDimensionPixelOffset(R.dimen.badged_user_picture_width);
        int badgedPictureHeight =
                resources.getDimensionPixelOffset(R.dimen.badged_user_picture_height);
        Bitmap badgedPicture = Bitmap.createBitmap(badgedPictureWidth, badgedPictureHeight,
                Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(badgedPicture);
        userPicture.setBounds(0, 0, imageSizePx, imageSizePx);
        userPicture.draw(canvas);

        // Cut a transparent hole through the background image.
        // This will serve as a border to the badge being overlaid.
        Paint paint = new Paint();
        paint.setAntiAlias(true);
        paint.setXfermode(new PorterDuffXfermode(PorterDuff.Mode.CLEAR));
        int badgeCenterX = badgedPictureWidth - badgeRadius;
        int badgeCenterY = badgedPictureHeight - badgeRadius;
        canvas.drawCircle(badgeCenterX, badgeCenterY, badgeRadius + borderSize, paint);

        // Draw the badge
        canvas.drawBitmap(badge, badgeCenterX - badgeRadius, badgeCenterY - badgeRadius, null);
        return new BitmapDrawable(resources, badgedPicture);
    }

    /**
     * Updates the user name and picture in the cache.
     *
     * @param context A context to get resources, current theme, etc.
     * @param accountId User's account id.
     * @param fullName User name.
     * @param bitmap User picture.
     */
    public static void updateUserNamePictureCache(
            Context context, String accountId, String fullName, Bitmap bitmap) {
        sChildAccountId = null;
        sCachedBadgedPicture = null;
        sToNamePicture.put(accountId, new Pair<>(fullName, makeRoundUserPicture(context, bitmap)));
    }

    /**
     * @param accountId An account.
     * @return A cached user name for a given account.
     */
    public static String getCachedUserName(String accountId) {
        Pair<String, Drawable> pair = sToNamePicture.get(accountId);
        return pair != null ? pair.first : null;
    }

    /**
     * Gets the user picture for the account from the cache, or returns the default picture if
     * unavailable.
     *
     * @param context A context to get resources, current theme, etc.
     * @param accountId A child account.
     * @return A user picture with badge for a given child account.
     */
    private static Drawable getBadgedUserPicture(Context context, String accountId) {
        if (sChildAccountId != null) {
            assert TextUtils.equals(accountId, sChildAccountId);
            return sCachedBadgedPicture;
        }
        sChildAccountId = accountId;
        Drawable picture = getUserPicture(context, accountId);
        Bitmap badge = BitmapFactory.decodeResource(
                context.getResources(), R.drawable.ic_account_child_20dp);
        sCachedBadgedPicture =
                overlayChildBadgeOnUserPicture(picture, badge, context.getResources());
        return sCachedBadgedPicture;
    }

    /**
     * Gets the user picture for the account from the cache, or returns the default picture if
     * unavailable.
     *
     * @param context A context to get resources, current theme, etc.
     * @param accountId Name of the account to get picture for.
     * @return A user picture for a given account.
     */
    public static Drawable getUserPicture(Context context, String accountId) {
        Pair<String, Drawable> pair = sToNamePicture.get(accountId);
        return pair != null ? pair.second : getAvatarPlaceholder(context);
    }

    private static Drawable getAvatarPlaceholder(Context context) {
        return AppCompatResources.getDrawable(context, R.drawable.logo_avatar_anonymous);
    }

    /**
     * Initiate fetching of an image and a picture of a given account. Fetched data will be sent to
     * observers of ProfileDownloader.
     *
     * @param context A context to get resources, current theme, etc.
     * @param profile A profile.
     * @param accountName An account name.
     */
    public static void startFetchingAccountInformation(
            Context context, Profile profile, String accountName) {
        if (TextUtils.isEmpty(accountName)) return;
        if (sToNamePicture.get(accountName) != null) return;

        final int imageSidePixels =
                context.getResources().getDimensionPixelOffset(R.dimen.user_picture_size);
        ProfileDownloader.startFetchingAccountInfoFor(
                context, profile, accountName, imageSidePixels, false);
    }

    /**
     * @return Whether the sign out is not disabled due to a child/EDU account.
     */
    private static boolean getSignOutAllowedPreferenceValue() {
        return ContextUtils.getAppSharedPreferences()
                .getBoolean(SIGN_OUT_ALLOWED, true);
    }

    /**
     * Sets the sign out allowed preference value.
     *
     * @param isAllowed True if the sign out is not disabled due to a child/EDU account
     */
    public static void setSignOutAllowedPreferenceValue(boolean isAllowed) {
        ContextUtils.getAppSharedPreferences()
                .edit()
                .putBoolean(SIGN_OUT_ALLOWED, isAllowed)
                .apply();
    }
}

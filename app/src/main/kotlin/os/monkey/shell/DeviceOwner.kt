package os.monkey.shell

import android.app.admin.DevicePolicyManager
import android.content.ComponentName
import android.content.Context
import android.os.UserManager
import android.util.Log

/**
 * The entire Kotlin -> DevicePolicyManager boundary, mirroring how [Native]
 * is the sole JNI boundary: nothing else in this app touches device-admin
 * APIs directly.
 *
 * Everything here is a no-op unless the app actually holds Device Owner
 * status (granted out-of-band via `adb shell dpm set-device-owner`, or
 * eventually QR provisioning — see docs/agents.md). On a device where that
 * was never granted, [isDeviceOwner] is false and [applyLockdown] does
 * nothing, so the app degrades to the pre-existing screen-pinning-only
 * behavior in [ShellActivity].
 */
object DeviceOwner {
    private const val TAG = "monkey"

    fun adminComponent(ctx: Context): ComponentName =
        ComponentName(ctx, KioskAdminReceiver::class.java)

    fun isDeviceOwner(ctx: Context): Boolean {
        val dpm = ctx.getSystemService(DevicePolicyManager::class.java) ?: return false
        return dpm.isDeviceOwnerApp(ctx.packageName)
    }

    /**
     * Allow-lists exactly [packages] for lock task and disables status bar /
     * notifications / home / overview outside it. Call with the same list
     * used to populate the grid, every time that list changes — this is the
     * actual security boundary for excluding an app (e.g. Settings): being
     * absent from the grid is UX hygiene, being absent from this list is
     * what stops it from starting at all, from any entry point.
     *
     * Safe to call when [isDeviceOwner] is false; every call is guarded and
     * failures are logged, not thrown, since losing device-owner status
     * (or never having had it) must not crash the launcher.
     */
    fun applyLockdown(ctx: Context, packages: List<String>) {
        val dpm = ctx.getSystemService(DevicePolicyManager::class.java) ?: return
        if (!dpm.isDeviceOwnerApp(ctx.packageName)) return
        val admin = adminComponent(ctx)

        runCatching {
            dpm.setLockTaskPackages(admin, packages.toTypedArray())
        }.onFailure { Log.w(TAG, "setLockTaskPackages failed", it) }

        runCatching {
            dpm.setLockTaskFeatures(admin, DevicePolicyManager.LOCK_TASK_FEATURE_NONE)
        }.onFailure { Log.w(TAG, "setLockTaskFeatures failed", it) }

        // Blocks the standard way to strip a device-admin app's protections
        // (booting into safe mode disables all non-system apps).
        runCatching {
            dpm.addUserRestriction(admin, UserManager.DISALLOW_SAFE_BOOT)
        }.onFailure { Log.w(TAG, "DISALLOW_SAFE_BOOT failed", it) }

        // Defense in depth against the Settings-UI reset path; moot once
        // Settings itself can't launch, but cheap and not mutually exclusive.
        runCatching {
            dpm.addUserRestriction(admin, UserManager.DISALLOW_FACTORY_RESET)
        }.onFailure { Log.w(TAG, "DISALLOW_FACTORY_RESET failed", it) }

        runCatching {
            dpm.setUninstallBlocked(admin, ctx.packageName, true)
        }.onFailure { Log.w(TAG, "setUninstallBlocked failed", it) }

        // Deliberately not set: DISALLOW_CONFIG_WIFI, DISALLOW_INSTALL_APPS,
        // DISALLOW_ADD_USER — open product decisions, not required for the
        // core lockdown. Also deliberately not set: DISALLOW_DEBUGGING_FEATURES
        // — this project's only install/update path is `adb`+installDebug;
        // disabling it would cut off maintenance with no store fallback.
    }
}

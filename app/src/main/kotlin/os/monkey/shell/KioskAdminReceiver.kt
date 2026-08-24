package os.monkey.shell

import android.app.admin.DeviceAdminReceiver
import android.content.Context
import android.content.Intent
import android.util.Log

/**
 * Anchor component for Device Owner status. `adb shell dpm set-device-owner`
 * targets this receiver directly, bypassing the normal interactive
 * "activate device admin" prompt, so there is nothing to do here beyond
 * existing and logging state changes. See docs/agents.md for the
 * provisioning procedure.
 */
class KioskAdminReceiver : DeviceAdminReceiver() {
    override fun onEnabled(context: Context, intent: Intent) {
        Log.i("monkey", "device admin enabled")
    }

    override fun onDisabled(context: Context, intent: Intent) {
        Log.i("monkey", "device admin disabled")
    }
}

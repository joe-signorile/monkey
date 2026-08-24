package os.monkey.shell

import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.PixelFormat
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.util.Log
import android.view.Gravity
import android.view.View
import android.view.WindowManager
import android.widget.Button
import android.widget.LinearLayout
import android.widget.TextView
import java.security.MessageDigest

// monkey-boy: no in-app way to change the PIN once set (clear app data to
// reset it) — upgrade if a parent-settings surface gets built for other
// reasons; not worth a screen on its own.

/**
 * The sole boundary for the escape-gap PIN gate, mirroring how [DeviceOwner]
 * is the sole Kotlin->DPM boundary. Screen pinning (invariant 8 in
 * docs/agents.md) blocks the shade/back/home/recents, but the standard unpin
 * gesture still exits it with no challenge — this closes that gap without
 * touching the device's real lock-screen credential (deliberately not used:
 * this is a shared family tablet, and setting a device-wide unlock PIN just
 * to gate one gesture was rejected).
 *
 * Mechanism: a `TYPE_APPLICATION_OVERLAY` window, not a launched Activity.
 * Android 10+ blocks starting activities from the background, and by the
 * time [ShellActivity.onStop] fires, this app already *is* background — but
 * adding a window isn't a "start," so the overlay still goes up reliably.
 * `Notification.setFullScreenIntent` was considered and rejected: Android
 * only actually takes it over the screen when locked/off, and this fires
 * with the screen on and unlocked (mid-escape), where it silently downgrades
 * to a dismissible heads-up banner.
 *
 * Requires the SYSTEM_ALERT_WINDOW special permission — granted once via
 * Settings, or `adb shell appops set os.monkey.shell SYSTEM_ALERT_WINDOW allow`.
 * No-op if it was never granted; a missing gate degrades to the pre-existing
 * screen-pinning-only behavior, same failure posture as [DeviceOwner].
 */
object PinGate {
    private const val TAG = "monkey"
    private const val PREFS = "pin_gate"
    private const val KEY_HASH = "hash"
    private const val PIN_LENGTH = 4

    private var overlay: View? = null

    fun hasOverlayPermission(ctx: Context): Boolean = Settings.canDrawOverlays(ctx)

    fun overlayPermissionIntent(ctx: Context): Intent =
        Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:${ctx.packageName}"))

    /**
     * Called from [ShellActivity.onStop] when lock task mode was exited
     * without [ShellActivity] itself calling `unlock()` first — i.e. the
     * unpin gesture, not a legitimate app hand-off via `launchApp`.
     */
    fun onUnauthorizedExit(ctx: Context) {
        if (overlay != null) return // already up; don't stack a second one
        if (!hasOverlayPermission(ctx)) {
            Log.w(TAG, "unauthorized exit but no overlay permission; cannot gate")
            return
        }
        show(ctx.applicationContext, setMode = !hasPin(ctx))
    }

    private fun prefs(ctx: Context) = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    private fun hash(pin: String): String =
        MessageDigest.getInstance("SHA-256").digest(pin.toByteArray())
            .joinToString("") { "%02x".format(it) }

    private fun hasPin(ctx: Context) = prefs(ctx).contains(KEY_HASH)

    private fun checkPin(ctx: Context, pin: String) = prefs(ctx).getString(KEY_HASH, null) == hash(pin)

    private fun setPin(ctx: Context, pin: String) {
        prefs(ctx).edit().putString(KEY_HASH, hash(pin)).apply()
    }

    private fun show(appCtx: Context, setMode: Boolean) {
        val wm = appCtx.getSystemService(WindowManager::class.java) ?: return
        val entered = StringBuilder()

        val prompt = TextView(appCtx).apply {
            text = if (setMode) "Set a PIN" else "Enter PIN"
            textSize = 20f
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
        }
        val dots = TextView(appCtx).apply {
            textSize = 32f
            setTextColor(Color.WHITE)
            gravity = Gravity.CENTER
        }
        fun refresh() {
            dots.text = "● ".repeat(entered.length) + "○ ".repeat(PIN_LENGTH - entered.length)
        }
        refresh()

        fun remove() {
            overlay?.let { v -> runCatching { wm.removeView(v) } }
            overlay = null
        }
        fun denyAndReturn() {
            remove()
            appCtx.startActivity(
                Intent(appCtx, ShellActivity::class.java)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT),
            )
        }
        fun onDigit(d: Char) {
            if (entered.length >= PIN_LENGTH) return
            entered.append(d)
            refresh()
            if (entered.length != PIN_LENGTH) return
            when {
                setMode -> { setPin(appCtx, entered.toString()); remove() }
                checkPin(appCtx, entered.toString()) -> remove()
                else -> { entered.clear(); refresh() } // wrong PIN: clear and retry, no lockout counter
            }
        }

        fun padButton(label: String, onClick: () -> Unit) = Button(appCtx).apply {
            text = label
            textSize = 22f
            setPadding(48, 32, 48, 32)
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT,
                LinearLayout.LayoutParams.WRAP_CONTENT,
            ).apply { setMargins(12, 12, 12, 12) }
            setOnClickListener { onClick() }
        }

        val pad = LinearLayout(appCtx).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            listOf(
                listOf("1", "2", "3"),
                listOf("4", "5", "6"),
                listOf("7", "8", "9"),
                listOf("Back", "0", "⌫"),
            ).forEach { row ->
                addView(
                    LinearLayout(appCtx).apply {
                        orientation = LinearLayout.HORIZONTAL
                        gravity = Gravity.CENTER
                        row.forEach { label ->
                            addView(
                                padButton(label) {
                                    when (label) {
                                        "Back" -> denyAndReturn()
                                        "⌫" -> if (entered.isNotEmpty()) {
                                            entered.deleteCharAt(entered.length - 1)
                                            refresh()
                                        }
                                        else -> onDigit(label[0])
                                    }
                                },
                            )
                        }
                    },
                )
            }
        }

        val root = LinearLayout(appCtx).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER
            setBackgroundColor(Color.argb(240, 0, 0, 0))
            addView(prompt)
            addView(dots)
            addView(pad)
        }

        val type = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY
        } else {
            @Suppress("DEPRECATION") WindowManager.LayoutParams.TYPE_PHONE
        }
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.MATCH_PARENT,
            type,
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL,
            PixelFormat.TRANSLUCENT,
        )

        overlay = root
        runCatching { wm.addView(root, params) }
            .onFailure { Log.w(TAG, "addView failed", it); overlay = null }
    }
}

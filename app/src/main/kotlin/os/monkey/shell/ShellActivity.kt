package os.monkey.shell

import android.app.Activity
import android.app.ActivityManager
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.MotionEvent
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowInsets
import android.view.WindowInsetsController
import java.util.concurrent.atomic.AtomicInteger

/**
 * The HOME activity. Owns the Surface and the Android framework calls; every
 * pixel and every gesture decision happens in C++ behind [Native].
 */
class ShellActivity : Activity(), SurfaceHolder.Callback {

    // Bumped by every loadCatalog() call; a background thread checks its own
    // snapshot against this after the expensive part of the load and drops
    // its results if it's been superseded, instead of racing another thread
    // to push to Native. See docs/agents.md invariant 9.
    private val catalogGeneration = AtomicInteger(0)

    // Screen-pinned whenever the grid is on screen; unpinned just long enough
    // to hand off to a launched app's task. See unlock()/relock().
    private var pinned = false

    // Every package handed off to since the grid was last on top; reaped in
    // onResume once it is (and the process is actually backgrounded). A set,
    // not a single slot: the launched app can itself hand off to a second app
    // (share sheet, "open with", a link) before the grid regains focus, and
    // that second package needs reaping too, not just the last one.
    private val launchedPkgs = mutableSetOf<String>()

    private val mainHandler = Handler(Looper.getMainLooper())

    // An install fires ADDED then REPLACED in quick succession; a debounced
    // single reload replaces both instead of racing two loadCatalog() calls
    // (which the generation guard would resolve correctly anyway, but
    // there's no reason to pay for two catalog scans when one will do).
    private val reloadCatalog = Runnable { loadCatalog() }

    // Context-registered, not manifest-declared: this only listens while
    // ShellActivity's process is alive, with zero standing cost otherwise.
    // A manifest <receiver> would keep it listening even with the launcher
    // fully torn down — exactly the always-on background footprint this
    // project otherwise has none of.
    private val packageChangeReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            mainHandler.removeCallbacks(reloadCatalog)
            mainHandler.postDelayed(reloadCatalog, PACKAGE_CHANGE_DEBOUNCE_MS)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        val view = SurfaceView(this)
        view.holder.addCallback(this)
        setContentView(view)

        Native.setActivity(this)
        loadCatalog()

        registerReceiver(
            packageChangeReceiver,
            IntentFilter().apply {
                addAction(Intent.ACTION_PACKAGE_ADDED)
                addAction(Intent.ACTION_PACKAGE_REMOVED)
                addAction(Intent.ACTION_PACKAGE_REPLACED)
                addDataScheme("package")
            },
        )
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) goImmersive()
    }

    override fun onResume() {
        super.onResume()
        // Best-effort memory reclaim: killBackgroundProcesses only acts once
        // Android considers the process backgrounded, which is now — we just
        // regained the foreground. A process with a foreground service
        // (media playback, a download) is exempt by design; that's the OS
        // protecting real work, not a bug to route around.
        if (launchedPkgs.isNotEmpty()) {
            val am = getSystemService(ActivityManager::class.java)
            launchedPkgs.forEach { am?.killBackgroundProcesses(it) }
            launchedPkgs.clear()
        }
        relock()
    }

    /** Reads the installed-app list off the main thread, then feeds native. */
    private fun loadCatalog() {
        val myGeneration = catalogGeneration.incrementAndGet()
        Thread {
            val started = System.nanoTime()
            val apps = AppCatalog.load(this)

            // Another loadCatalog() started after this one; our results are
            // stale. Drop them rather than race the newer thread into Native.
            if (myGeneration != catalogGeneration.get()) return@Thread

            Native.clearApps()
            apps.forEach {
                Native.addApp(
                    it.label, it.pkg,
                    it.iconPixels, AppCatalog.ICON_PX, AppCatalog.ICON_PX,
                    it.labelPixels, AppCatalog.LABEL_W, AppCatalog.LABEL_H,
                )
            }
            Native.appsReady()
            // No-op unless this app holds Device Owner status; see DeviceOwner.kt.
            // Same list that just went into the grid, so the lock task
            // allow-list and what's actually shown never drift apart.
            DeviceOwner.applyLockdown(this, apps.map { it.pkg })
            Log.i(TAG, "catalog: ${apps.size} apps in ${(System.nanoTime() - started) / 1_000_000}ms")
        }.start()
    }

    /**
     * Called from the native render thread on tap. startActivity must run on
     * the main thread, so hop back before launching.
     */
    @Suppress("unused") // invoked via JNI; see proguard-rules.pro
    fun launchApp(pkg: String) = runOnUiThread {
        val intent = packageManager.getLaunchIntentForPackage(pkg)
        if (intent == null) {
            // App was uninstalled since the catalog was built.
            Log.w(TAG, "no launch intent for $pkg; refreshing catalog")
            loadCatalog()
            return@runOnUiThread
        }

        // A pinned task refuses to hand off to a new one; unpin for the
        // duration of the launched app, then relock() re-engages it in
        // onResume once the grid is back on top.
        unlock()
        launchedPkgs += pkg
        runCatching { startActivity(intent) }
            .onFailure {
                Log.w(TAG, "failed to launch $pkg", it)
                launchedPkgs -= pkg
                relock() // startActivity never left the foreground; re-pin now
            }
    }

    /** Screen-pins the grid: no shade, no back/home/recents until unlock(). */
    private fun relock() {
        if (pinned) return
        runCatching { startLockTask() }
            .onSuccess { pinned = true }
            .onFailure { Log.w(TAG, "startLockTask failed", it) }
    }

    private fun unlock() {
        if (!pinned) return
        runCatching { stopLockTask() }
            .onSuccess { pinned = false }
            .onFailure { Log.w(TAG, "stopLockTask failed", it) }
    }

    override fun onStop() {
        super.onStop()
        // A stop while we never called unlock() means something exited lock
        // task mode out from under us — the unpin gesture, not a legitimate
        // launchApp() hand-off. Check the real OS state, not just onStop()
        // firing: a transient system dialog covering the grid also triggers
        // onStop() without actually leaving lock task, and must not trip the
        // gate. See PinGate for why this needs an overlay, not an activity.
        val am = getSystemService(ActivityManager::class.java)
        if (pinned && am?.lockTaskModeState != ActivityManager.LOCK_TASK_MODE_PINNED) {
            pinned = false // let the next onResume()'s relock() actually re-engage
            PinGate.onUnauthorizedExit(this)
        }
    }

    override fun surfaceCreated(holder: SurfaceHolder) = Unit

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        Native.surfaceCreated(holder.surface, width, height)
    }

    override fun surfaceDestroyed(holder: SurfaceHolder) {
        // Must block until the render thread releases the ANativeWindow;
        // returning early lets the framework free a surface still in use.
        Native.surfaceDestroyed()
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        Native.touch(event.actionMasked, event.x, event.y)
        return true
    }

    override fun onDestroy() {
        unregisterReceiver(packageChangeReceiver)
        mainHandler.removeCallbacksAndMessages(null)
        Native.setActivity(null)
        super.onDestroy()
    }

    // A launcher is the bottom of the stack; back must not exit it.
    @Deprecated("Base Activity has no OnBackPressedDispatcher; this is the API here.")
    override fun onBackPressed() = Unit

    private fun goImmersive() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false)
            window.insetsController?.apply {
                hide(WindowInsets.Type.systemBars())
                systemBarsBehavior =
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
            }
        } else {
            @Suppress("DEPRECATION")
            window.decorView.systemUiVisibility =
                View_SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
                    View_SYSTEM_UI_FLAG_FULLSCREEN or
                    View_SYSTEM_UI_FLAG_HIDE_NAVIGATION or
                    View_SYSTEM_UI_FLAG_LAYOUT_STABLE or
                    View_SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
                    View_SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
        }
    }

    private companion object {
        const val TAG = "monkey"
        const val PACKAGE_CHANGE_DEBOUNCE_MS = 300L

        // android.view.View constants, inlined to avoid the deprecated imports.
        const val View_SYSTEM_UI_FLAG_FULLSCREEN = 0x00000004
        const val View_SYSTEM_UI_FLAG_HIDE_NAVIGATION = 0x00000002
        const val View_SYSTEM_UI_FLAG_IMMERSIVE_STICKY = 0x00001000
        const val View_SYSTEM_UI_FLAG_LAYOUT_STABLE = 0x00000100
        const val View_SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN = 0x00000400
        const val View_SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION = 0x00000200
    }
}

package os.monkey.shell

import android.view.Surface
import java.nio.ByteBuffer

/**
 * The entire Kotlin -> C++ boundary. Nothing else in the Kotlin layer talks to
 * native code. Signatures here must stay in sync with jni_bridge.cpp; the JNI
 * names are derived from this package + object name.
 */
object Native {
    init { System.loadLibrary("monkeyshell") }

    /** Global ref the render thread calls back into. Pass null on teardown. */
    external fun setActivity(activity: Any?)

    /** Also handles resize: the renderer tears down and restarts on each call. */
    external fun surfaceCreated(surface: Surface, width: Int, height: Int)
    external fun surfaceDestroyed()

    external fun clearApps()

    /**
     * [iconPixels] and [labelPixels] must be *direct* ByteBuffers of
     * RGBA_8888, width*height*4 bytes each.
     */
    external fun addApp(
        label: String,
        pkg: String,
        iconPixels: ByteBuffer,
        iconWidth: Int,
        iconHeight: Int,
        labelPixels: ByteBuffer,
        labelWidth: Int,
        labelHeight: Int,
    )

    /** Signals the catalog is fully loaded and the grid may lay out. */
    external fun appsReady()

    /** [action] is the raw MotionEvent action constant. */
    external fun touch(action: Int, x: Float, y: Float)
}

package os.monkey.shell

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Color
import android.text.TextPaint
import android.text.TextUtils
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.nio.ByteBuffer

/** One launchable app: display label, package to launch, rasterized icon + label textures. */
class AppEntry(
    val label: String,
    val pkg: String,
    val iconPixels: ByteBuffer,
    val labelPixels: ByteBuffer,
)

object AppCatalog {
    /** Fixed raster size. Every icon becomes a same-sized GL texture. */
    const val ICON_PX = 128

    /** Fixed raster size for the label strip drawn under each icon. */
    const val LABEL_W = 256
    const val LABEL_H = 48

    private const val ICON_BYTES = ICON_PX * ICON_PX * 4
    private const val LABEL_BYTES = LABEL_W * LABEL_H * 4
    private const val LABEL_TEXT_SIZE = 26f

    // Never shown as a tile. The real boundary against reaching these is
    // DeviceOwner.applyLockdown's lock-task allow-list, which this package
    // is never part of; this filter is UX hygiene so a blocked package
    // doesn't sit in the grid as a tile that silently refuses to launch.
    // monkey-boy: hardcoded AOSP package name — confirm it matches on Fire
    // OS during hardware verification (see docs/agents.md); add the actual
    // package there if it differs.
    private val HIDDEN_PACKAGES = setOf("com.android.settings")

    /**
     * Enumerates every launchable app and rasterizes its icon, using a disk
     * cache keyed by (package, lastUpdateTime) to skip rasterization on
     * repeat cold starts — see [iconCacheFile].
     *
     * Blocking and allocation-heavy on a cache miss — call off the main
     * thread.
     *
     * Relies on the <queries> block in AndroidManifest.xml. Without it this
     * silently returns a near-empty list on API 30+.
     */
    fun load(ctx: Context): List<AppEntry> {
        val pm = ctx.packageManager
        val self = ctx.packageName
        val intent = Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_LAUNCHER)
        val cacheDir = File(ctx.cacheDir, "icons").apply { mkdirs() }

        // One scratch bitmap reused across every cache-miss icon; the pixel
        // bytes are copied out per entry, so the bitmap itself never escapes.
        val scratch = Bitmap.createBitmap(ICON_PX, ICON_PX, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(scratch)
        val wantedFiles = HashSet<String>()

        // Labels aren't disk-cached like icons -- Canvas.drawText on a
        // ~50px-tall bitmap is cheap next to icon rasterization (which
        // includes AdaptiveIconDrawable compositing), so there's no budget
        // pressure to cache against.
        val labelScratch = Bitmap.createBitmap(LABEL_W, LABEL_H, Bitmap.Config.ARGB_8888)
        val labelCanvas = Canvas(labelScratch)
        val labelPaint = TextPaint(TextPaint.ANTI_ALIAS_FLAG).apply {
            color = Color.WHITE
            textAlign = android.graphics.Paint.Align.CENTER
            textSize = LABEL_TEXT_SIZE
        }

        val apps = pm.queryIntentActivities(intent, 0)
            .asSequence()
            // Hiding ourselves stops the launcher from listing itself as a tile.
            .filter { it.activityInfo.packageName != self }
            .filter { it.activityInfo.packageName !in HIDDEN_PACKAGES }
            .distinctBy { it.activityInfo.packageName }
            .mapNotNull { info ->
                val pkg = info.activityInfo.packageName
                // A package with no launch intent cannot be started; drop it
                // rather than render a tile that does nothing when tapped.
                pm.getLaunchIntentForPackage(pkg) ?: return@mapNotNull null

                val lastUpdate = runCatching { pm.getPackageInfo(pkg, 0).lastUpdateTime }
                    .getOrDefault(0L)
                val cacheFile = iconCacheFile(cacheDir, pkg, lastUpdate)
                wantedFiles += cacheFile.name

                val iconBuf = readCache(cacheFile) ?: run {
                    scratch.eraseColor(0)
                    // loadIcon handles AdaptiveIconDrawable compositing for us.
                    info.loadIcon(pm).apply {
                        setBounds(0, 0, ICON_PX, ICON_PX)
                        draw(canvas)
                    }
                    // ARGB_8888 is RGBA byte order in memory, which is exactly
                    // what GL_RGBA expects, so this uploads with no swizzle.
                    ByteBuffer.allocateDirect(ICON_BYTES).also {
                        scratch.copyPixelsToBuffer(it)
                        it.rewind()
                        writeCache(cacheFile, it)
                        it.rewind()
                    }
                }

                val label = info.loadLabel(pm).toString()
                val labelBuf = rasterizeLabel(label, labelScratch, labelCanvas, labelPaint)

                AppEntry(label, pkg, iconBuf, labelBuf)
            }
            .sortedBy { it.label.lowercase() }
            .toList()
            .also {
                scratch.recycle()
                labelScratch.recycle()
            }

        // A stale entry (app updated or uninstalled since it was cached)
        // isn't in this run's wanted set; drop it so the cache doesn't grow
        // without bound across updates.
        cacheDir.listFiles()?.forEach { if (it.name !in wantedFiles) it.delete() }

        return apps
    }

    private fun iconCacheFile(cacheDir: File, pkg: String, lastUpdateTime: Long) =
        File(cacheDir, "$pkg-$lastUpdateTime.rgba")

    /**
     * Draws [label], ellipsized to fit, centered into a fixed LABEL_W x
     * LABEL_H bitmap. Reuses the same Canvas/Paint rasterization pattern as
     * the icon path above, so Android's own text shaping/i18n/font fallback
     * does the work instead of a hand-rolled bitmap-font renderer.
     */
    private fun rasterizeLabel(label: String, scratch: Bitmap, canvas: Canvas, paint: TextPaint):
        ByteBuffer {
        scratch.eraseColor(0)
        val text = TextUtils.ellipsize(label, paint, LABEL_W * 0.92f, TextUtils.TruncateAt.END)
        val x = LABEL_W / 2f
        val y = LABEL_H / 2f - (paint.descent() + paint.ascent()) / 2f
        canvas.drawText(text, 0, text.length, x, y, paint)

        return ByteBuffer.allocateDirect(LABEL_BYTES).also {
            scratch.copyPixelsToBuffer(it)
            it.rewind()
        }
    }

    private fun readCache(file: File): ByteBuffer? {
        if (file.length() != ICON_BYTES.toLong()) return null
        val buf = ByteBuffer.allocateDirect(ICON_BYTES)
        return runCatching {
            FileInputStream(file).channel.use { channel ->
                while (buf.hasRemaining()) {
                    if (channel.read(buf) < 0) return null // truncated file
                }
            }
            buf.rewind()
            buf
        }.getOrNull()
    }

    private fun writeCache(file: File, pixels: ByteBuffer) {
        val dup = pixels.duplicate().also { it.rewind() } // don't disturb the caller's buffer
        runCatching {
            FileOutputStream(file).channel.use { channel ->
                while (dup.hasRemaining()) channel.write(dup)
            }
        }
    }
}

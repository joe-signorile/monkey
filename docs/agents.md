<!-- monkey-boy: markdown reference — upgrade to TOON when C++ exceeds ~10 translation units -->

# monkey — agent reference

A kid-facing Android launcher. Replaces the Amazon Kids home experience with a
GLES2-rendered grid of installed apps.

## What this is and is not

It is a **launcher**: a HOME-role app that queries `PackageManager` and fires `Intent`s.
Nothing is virtualized; Android's ActivityManager does the process work.

It is **not** a custom OS and cannot become one on target hardware. Fire HD 8 12th-gen
(2022), Fire 7 12th-gen (2022, codename `quartz`/KFQUWI), and all current Kids models ship
verified boot `green` with no public bootloader exploit — confirmed live against a `quartz`
unit (Fire OS 8 / Android 11, Sept-2025 patch): `ro.boot.flash.locked=1`,
`ro.boot.verifiedbootstate=green`, no known root path. A custom ROM/distro needs the same
missing exploit (`fastboot oem unlock` has nothing to unlock without one), so it isn't a
fallback either. Being a plain Android app is what makes it run at all.

This rules out a hypervisor/sandbox model where launched apps run inside something
monkey owns — that needs root or an unlockable bootloader, neither of which exists on
this hardware.

Device Owner (`setLockTaskPackages`, system-wide lock task with no user-escape gesture) is
the real ceiling *if it's reachable*. The app already carries dormant scaffolding for it —
`KioskAdminReceiver` (a `DeviceAdminReceiver`, manifest-registered with a no-policy
`device_admin.xml`) and `DeviceOwner.applyLockdown()`, the sole Kotlin->DPM boundary. Both
are no-ops unless the app actually holds Device Owner status; absent that, the app degrades
to screen-pinning-only. Confirmed live on a fully-provisioned `quartz` unit,
`adb shell dpm set-device-owner os.monkey.shell/.KioskAdminReceiver`:

```
IllegalStateException: Trying to set the device owner, but the user already has a profile owner.
```

So on an already-set-up Fire tablet this is a hard no — `com.amazon.parentalcontrols` is
Profile Owner on every user including the adult profile (`dumpsys device_policy`), and
Android refuses Device Owner once *any* Profile Owner exists anywhere. What's still open:
whether a genuinely fresh, factory-reset unit — before any account is added — would already
have `parentalcontrols` provisioned as Profile Owner by the time Developer
Options/USB debugging becomes available, which would close the enrollment window from the
start of OOBE regardless of account state. Third-party Fire OS MDM vendors (Hexnode)
advertise Device Owner support for Fire tablets, positive signal, not proof for this
generation. Settling it for real means factory-resetting a throwaway unit and racing
`dpm set-device-owner` before OOBE finishes — destructive, not attempted here. Don't assume
the factory-reset story in the fleet-dashboard plan (Section B, not yet written) is
buildable until that's checked.

Until then, the ceiling actually in hand — no root, no Device Owner, no factory reset — is
screen pinning (`LOCK_TASK_MODE_PINNED`, invariant 8) plus an in-app PIN gate (`PinGate.kt`):
rather than the device's real lock-screen credential
(`Settings.Secure.lock_to_app_exit_locked` — works, but means setting an actual unlock PIN on
a shared family device, rejected as a precondition), `ShellActivity.onStop` detects the app
losing the pinned foreground and `PinGate` throws a `TYPE_APPLICATION_OVERLAY` window over
whatever surfaced before it's usable — wrong/no PIN snaps back to the locked grid, right PIN
lets the exit stand. Built and confirmed live end-to-end (Set-PIN flow, overlay survives a
real unpin via `am task lock stop`); see invariant 8 for the one real-hardware surprise this
surfaced. If the target hardware ever changes (unlockable bootloader) or the OOBE question
resolves favorably, Device Owner is worth re-opening; on a stock, already-provisioned Fire
tablet, neither root nor Device Owner is buildable today, and no amount of app-layer
engineering closes that gap on its own.

## Architecture

Thin Kotlin host, C++ core. Kotlin touches the Android framework and nothing else; all
layout, rendering and gesture logic is C++.

| File | Responsibility |
| --- | --- |
| `app/src/test/cpp/grid_test.cpp` | Host-side checks for scroll physics and hit-testing. |
| `ShellActivity.kt` | HOME activity. Owns the Surface, immersive mode, touch forwarding, `launchApp`. |
| `AppCatalog.kt` | `PackageManager` query + icon rasterization. Blocking; runs off the main thread. |
| `Native.kt` | The entire Kotlin→C++ boundary. Nothing else calls native. |
| `jni_bridge.cpp` | JNI entry points, `ShellActivity` global ref, the single `Renderer`. |
| `renderer.{h,cpp}` | EGL, GL program, textures, render thread, idle gating. |
| `grid.{h,cpp}` | Tile layout, scroll physics, hit-testing. Pure geometry; no GL, no JNI. |

### Threading

- **UI thread** handles all touch. `Renderer::onTouch` returns a package name on tap and
  `jni_bridge` calls straight back into `ShellActivity.launchApp`. The caller is already
  attached, so **no `AttachCurrentThread` anywhere in this codebase**. Keep it that way.
- **Render thread** owns EGL and every GL call. It never calls into Java.
- **Catalog thread** builds the app list, then pushes it through `Native.addApp`.
- `Grid` is *not* internally synchronized. `Renderer::mutex_` guards it. Any new `Grid`
  access must be under that lock.

### Invariants that fail silently

These produce no error — just wrong behaviour. Do not "clean them up".

1. **`<queries>` in `AndroidManifest.xml`.** Without the MAIN/LAUNCHER intent block, API
   30+ package-visibility filtering makes `queryIntentActivities` return a near-empty list.
   The grid comes up blank with nothing in logcat.
2. **`glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA)`.** Canvas-drawn `ARGB_8888` is
   *premultiplied*. The reflexive `GL_SRC_ALPHA` double-darkens every icon edge.
3. **`vUV = uUV.xy + aPos * uUV.zw`, no flip.** `glTexImage2D`/`glTexSubImage2D` map the
   first data row (the bitmap's top) to `v=0`, so the quad's local coords map straight onto
   the atlas cell — no vertical flip needed. `uUV` is the tile's cell rect within the shared
   atlas (see invariant 11); dropping it back to `vUV = aPos` samples the whole atlas
   through every tile instead of one cell.
4. **Icon pixels are retained after upload.** EGL context loss invalidates the atlas
   texture; the retained ~64KB per app makes recovery a re-upload instead of a full
   re-query. Do not "optimize" by freeing them.
5. **`Renderer::stop()` blocks.** `surfaceDestroyed` depends on it; returning early lets
   the framework free a surface the GL thread is still drawing into.
6. **`proguard-rules.pro` keeps `Native` and `launchApp`.** R8 cannot see JNI call sites.
7. **Zero AndroidX.** `ShellActivity` extends `android.app.Activity`; the theme derives
   from `@android:style/Theme.Material.NoActionBar.Fullscreen`. This is what holds the APK
   size budget. Adding an AndroidX dependency breaks it.
8. **`pinned` around `startActivity`.** Screen pinning (`startLockTask`/`stopLockTask`)
   blocks the shade, back, home and recents while the grid is on screen — the closest
   this app gets to a locked-down kiosk without root or Device Owner provisioning. A
   pinned task cannot hand off to a new one, so `launchApp` must call `unlock()` before
   `startActivity`; skipping that makes every tap silently no-op (or toast-refuse) instead
   of launching. `relock()` in `onResume` re-engages it once the grid is back on top. This
   is grid-level only: apps launched from the grid run unpinned, with normal back/home, so
   a kid can always return to the launcher. True system-wide lock task (spanning every
   installed app) needs Device Owner + `setLockTaskPackages` — confirmed blocked on an
   already-provisioned Fire tablet, see "What this is and is not" above — and even if
   reachable on a fresh unit, factory-reset provisioning breaks the "sideload onto an adult
   profile" install story in the README, so it's out of scope unless that tradeoff is
   chosen deliberately. The escape gap this leaves (the standard unpin gesture) is closed
   without Device Owner by `PinGate.kt`: `onStop` detects the loss of pinned foreground and
   throws a full-screen PIN overlay over whatever surfaced.

   **Real-hardware trap found while wiring this up:** `startLockTask()` returns
   successfully (no exception) even when the OS never actually enters
   `LOCK_TASK_MODE_PINNED` — confirmed live, `mLockTaskModeState` stayed `NONE` and Home
   worked normally despite `onSuccess` firing. The cause: stock Android's one-time "App is
   pinned" system dialog (Back+Overview instructions, "NO THANKS"/"GOT IT") has to be
   dismissed by an actual touch before pinning finalizes; `startLockTask()`'s return value
   only reflects that the *request* was accepted, not that pinning is active. Automated
   testing over adb (`input keyevent`, no real touch) never dismisses it, so it's easy to
   conclude pinning is broken when it's actually just pending a tap. Real touches on real
   hardware hit this once per device (or per app-data-clear); nothing to build around, just
   don't mistake `onSuccess` for "actually pinned" when testing headlessly.
9. **`catalogGeneration` in `ShellActivity`.** `loadCatalog()` can fire twice close
   together (cold start racing a retry from an uninstalled-package tap, or a debounced
   package-change broadcast). The background thread checks its generation snapshot against
   the current value after `AppCatalog.load()` and before touching `Native`; a superseded
   thread returns without calling `clearApps`/`addApp`/`appsReady`. Removing the check
   re-opens a two-thread race where both push to `Native` with no ordering guarantee —
   duplicate tiles, not a crash, so it fails silently.
10. **`packageChangeReceiver` is context-registered, not in the manifest.** Declaring
    `ACTION_PACKAGE_ADDED`/`_REMOVED`/`_REPLACED` as a manifest `<receiver>` would keep it
    listening even with the launcher process fully torn down — a standing background cost
    this project has deliberately had none of. Registering it in `onCreate` and
    unregistering in `onDestroy` scopes it to exactly the launcher's own lifetime, which
    for a HOME app is effectively always anyway.
11. **`Renderer::freeSlots_` is CPU state, not GL state.** Unlike the old one-texture-per-icon
    scheme, freeing an atlas slot (`clearApps`) is just an index pushed onto a vector — no GL
    call, so it's safe from the UI thread with no GL context. Only the atlas texture itself
    (`atlasTexture_`) is a GL object, owned and touched by the render thread alone. Atlas
    capacity is fixed at `kAtlasCapacity` (256) cells; `uploadPending()` logs and drops the
    tile's texture rather than crash if that's ever exceeded — see the `monkey-boy:` comment
    there for the upgrade trigger.

### JNI contract

Kotlin `object` members compile to *instance* methods, so every entry point takes
`jobject`, not `jclass`. Symbol names derive from `os.monkey.shell.Native`; changing
either side alone gives an `UnsatisfiedLinkError` at first touch.

`setActivity`, `surfaceCreated`, `surfaceDestroyed`, `clearApps`, `addApp`, `appsReady`,
`touch`.

Verify both sides still agree:

```
nm -D --defined-only app/build/intermediates/cxx/Debug/*/obj/arm64-v8a/libmonkeyshell.so \
  | grep ' T Java_' | awk '{print $3}' | sort
```

## Test

```
./tools/run-grid-test.sh
```

Compiles `grid.cpp` against g++ and runs it — no Android toolchain, no device. This exists
because **scroll cannot be exercised on a stock emulator**: a bare image has ~17 apps,
which fits one screen, so `maxScroll()` is 0 and every scroll path is dead code at runtime.
`Grid` is kept free of GL and JNI specifically so this stays possible. Keep it that way.

The suite is mutation-checked: removing the gutter-rejection line in `hitTest` makes it
fail.

## Build

Requires JDK 21 (pinned in `gradle.properties`; AGP 8.7 is not validated against JDK 25).

```
./gradlew :app:assembleDebug
./gradlew :app:installDebug
```

Toolchain: AGP 8.7.3, Gradle 8.11.1, Kotlin 2.0.21, NDK 27.3.13750724, cmake 3.22.1.
ABIs: `arm64-v8a`, `armeabi-v7a`, `x86_64`. `minSdk 23`, `targetSdk 34`.

## Run

```
adb shell cmd package set-home-activity os.monkey.shell/.ShellActivity
adb shell input keyevent KEYCODE_HOME
adb logcat -s monkey          # "catalog: N apps in Xms"
```

On a real Fire tablet: `installDebug` over ADB, then set HOME via Settings → Apps →
Default Apps. **Adult profile only** — Amazon Kids profiles are enforced by a
Device/Profile Owner and will not honour a custom launcher without root.

## Performance budget

The reason this project exists. Measure, do not estimate.

| Metric | Target | Measured | How |
| --- | --- | --- | --- |
| Cold start → first frame | < 500 ms | stale (486 ms pre-atlas) | `adb shell am start -W` |
| Scroll frame time | ≤ vsync, no drops | not yet measured | `adb shell dumpsys gfxinfo os.monkey.shell` |
| Catalog load (cold cache) | < 300 ms | stale (186 ms pre-disk-cache, 17 apps) | logcat `monkey` |
| Catalog load (warm cache) | faster than cold | not yet measured | logcat `monkey`, second launch |
| Release APK size | < 2 MB | stale (1.07 MB pre-atlas/labels) | `ls -l` on the release APK |

The disk icon cache, texture/label atlases, and label rasterization all landed after the row
above was last measured — every "stale" row needs a real run on hardware before it's trusted
again. Nothing here is expected to regress (the atlas replaces N texture binds with 2; the
disk cache only adds work on the very first cold start), but "expected" isn't "measured".

Measured on an `android-34` x86_64 emulator with software GL — real hardware should beat
these. `classes.dex` is 17 KB; that is the zero-AndroidX rule paying for itself, and it is
why adding an AndroidX dependency is called out as an invariant above.

The render thread blocks on a condition variable when nothing is moving. An idle launcher
must cost zero CPU; if you add a frame trigger, make sure it clears itself.

## Known gaps (not yet an MVP)

Not deliberate cuts (see Deferred below for those) — just not proven yet. Ranked by risk:

1. **Reboot persistence is unverified.** HOME was set on real hardware via manual
   Settings → Apps → Default Apps, because `cmd package set-home-activity` silently
   no-ops on this device (see Run). Nobody has power-cycled the tablet and confirmed
   monkey is still HOME afterward instead of Fire OS reverting to
   `com.amazon.firelauncher`. This is the single biggest open risk for an unattended kid
   device — untested, not assumed either way.
2. **`PinGate`'s wrong/correct-PIN branch (`setMode = false`) was never independently
   re-confirmed on device with a screenshot** — only the Set-PIN path was (see invariant
   8). The code is structurally identical (same `onDigit`/`checkPin`/`remove` flow as
   Set-PIN), so risk is low, but this project's own bar elsewhere is "measure, do not
   estimate," and that bar hasn't been met here yet.
3. **`PinGate.overlayPermissionIntent()` is dead code.** `SYSTEM_ALERT_WINDOW` is granted
   once via `adb shell appops set os.monkey.shell SYSTEM_ALERT_WINDOW allow` (see
   Install) — there's no in-app fallback if it's ever revoked (app-data clear, Android's
   automatic revocation of unused special permissions). A parent with no adb access would
   have no way back in short of reinstalling. The intent builder exists for exactly this
   case and nothing calls it yet; wiring it means checking
   `PinGate.hasOverlayPermission()` in `ShellActivity.onCreate` and routing to
   `overlayPermissionIntent()` if it's missing.
4. **Performance budget table below is stale for every row except cold start, and even
   that predates the atlas.** Scroll frame time and warm-cache load have never been
   measured against this codebase's current form. "Measure, do not estimate" is this
   project's stated reason to exist; the table hasn't lived up to it since the atlas
   landed.
5. **No `android:icon` on `<application>`.** Shows as a blank/generic icon in
   Settings → Apps and the Default Apps picker. Cosmetic, not functional, but the first
   thing a parent sees when trying to identify which app is monkey in a list.

## Deferred

Tracked, deliberate omissions:

- Guaranteed process kill on app exit — `killBackgroundProcesses` in
  `ShellActivity.onResume` is a hint, not a force-stop; it only reaps a process once
  Android considers it backgrounded, and it skips anything holding a foreground service
  (media playback, a download). A true always-kill needs `FORCE_STOP_PACKAGES`
  (signature-permission, not grantable to a third-party app) or root — neither fits this
  project's no-root install model.

# monkey

A kid-facing Android launcher. It replaces the home screen with a large, vertically
scrolling grid of the apps already installed on the tablet — and nothing else.

Built for Amazon Fire Kids tablets, but it is a normal Android app, so it runs anywhere.

## Why

The Amazon Kids app carries a content service, DRM, account sync, telemetry and a
web-hybrid UI. Most of that is overhead a parent never asked for, and it shows in how the
thing feels. monkey drops all of it and renders the grid directly in OpenGL ES.

The performance targets are written down and measured, not asserted — see
`docs/agents.md`.

## What it is

A **launcher**. It lists every installed app, and tapping one starts it through the normal
Android intent. Apps keep their own data; nothing is sandboxed, copied or virtualized.

## What it is not

**It is not a custom OS, and it cannot be flashed.** Fire HD 8 12th-gen (2022) and every
current Kids model ship a locked bootloader with no public exploit. That is fine: a
sideloaded launcher already gets the whole point — every installed app, launchable, with
saved data intact, no root required.

## Requirements

- JDK 21 (the build pins it; newer JDKs are not validated against AGP 8.7)
- Android SDK with NDK 27.3.13750724 and cmake 3.22.1

## Install

```
./gradlew :app:installDebug
adb shell cmd package set-home-activity os.monkey.shell/.ShellActivity
adb shell input keyevent KEYCODE_HOME
adb shell appops set os.monkey.shell SYSTEM_ALERT_WINDOW allow
```

The last line grants the PIN-gate overlay permission (see Status below) — without it,
`PinGate` logs a warning and does nothing, degrading to screen-pinning-only. The first time
`startLockTask()` actually runs, Android shows a one-time "App is pinned" system dialog that
needs a real touch (`GOT IT`) to finalize — see invariant 8 in `docs/agents.md` if pinning
looks broken when testing headlessly over adb.

To go back to the stock launcher, set the home activity back, or clear defaults in
Settings → Apps.

## Important caveat: Kids profiles

Amazon enforces Kids profiles through a Device/Profile Owner. **A custom launcher applies
to an adult profile only** and cannot be installed into a Kids profile without root.

The working model is a dedicated adult profile that *is* the kid experience — monkey as
the home screen, with only the apps you want present on the device.

## Status

The grid lists apps (icon + label), scrolls with rubber-band overscroll, launches them,
and stays live as apps are installed/updated/removed. Icons are disk-cached across cold
starts. Screen pinning holds the launcher while the grid is on screen; a launched app runs
unpinned, with normal back/home, so a kid can always return. The standard unpin gesture
(hold Back+Recents) is closed by an in-app PIN prompt: `ShellActivity` notices the app
losing the pinned foreground and `PinGate` throws a full-screen PIN overlay over whatever
surfaced. The Set-PIN path is confirmed working end-to-end on device (screenshots taken
through a real unpin → overlay → stored PIN → re-lock cycle); the wrong/correct-PIN
check path is structurally identical but hasn't been independently re-confirmed on device
yet. System-wide lockdown that holds across launched apps too (Device
Owner) is confirmed blocked on an already-provisioned Fire tablet — Fire OS runs its own
Profile Owner (`com.amazon.parentalcontrols`) on every user, including the adult profile,
and Android refuses Device Owner once any Profile Owner exists — see "What this is and is
not" in `docs/agents.md` for the live error and what's still open (a from-scratch,
factory-reset device is untested).

Not yet an MVP: reboot persistence of the HOME assignment is untested (the biggest open
risk for a device meant to run unattended), the overlay-permission grant has no in-app
recovery path if it's ever revoked, and the performance budget hasn't been measured on
real hardware since the icon/label atlas landed. Full list, ranked by risk, in
`docs/agents.md` under "Known gaps."

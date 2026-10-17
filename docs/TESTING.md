# Testing airShot

## Workstation

airShot is built and tested on the X399 Haiku workstation (192.168.1.244):

- `tools/ws.sh '<command>'` runs a command there over SSH.
- `tools/sync-build.sh [target]` syncs the tree to `/boot/home/airshot` and
  runs `make` there, printing errors only.
- `tools/install-dev.sh [--run]` installs the filter into
  `~/config/non-packaged/add-ons/input_server/filters/airShot` and the app into
  `~/config/non-packaged/apps/airShot`, optionally starting it.
- `tools/sync-build.sh package` builds the hpkg in `artifacts/`;
  `pkgman install -y <hpkg>` installs it (bump the version first when
  reinstalling). Remove a dev install first (`desklink --remove=airShot`, the
  files under `~/config/non-packaged`), or two filters will be loaded.
- `tools/build-cross.sh` cross-compiles on Linux with the x399 Haiku build
  tree, for quick compile checks when the workstation is unreachable.

The workstation's VNC server (port 5900) injects input through the
`InputEventInjector` device, so keystrokes sent over VNC pass through
`input_server` and therefore through the shortcut filter, which makes the
shortcuts testable remotely:

```
python3 tools/vnc.py shot out.png --scale 0.5     # screenshot
python3 tools/vnc.py key 0xFF61                    # Print Screen
python3 tools/vnc.py click X Y / drag X0 Y0 X1 Y1 / type "text"
```

The NanoKVM at 192.168.1.22 can send real USB keyboard events as a second
path (`/mnt/HaikuWork/x399/tools/kvm.py`).

`tools/vnc-script.sh` runs a list of `vnc.py` commands (one per line, with
`shot NAME X,Y,W,H` and `full NAME SCALE` for captures). Start the app with
`AIRSHOT_TRACE=1` (what `tools/install-dev.sh --run` does) to get message and
overlay traces in `/boot/home/airshot/app.log`; `touch /boot/home/airshot/.trace`
makes the filter log every key press and dispatch to the syslog. The
workstation's `debug_server` is set to write crash reports for airShot to the
Desktop without a dialog (`~/config/settings/system/debug_server/settings`).

Note that the VNC server (and `BScreen::GetBitmap`) did not show the overlay
window at all while a Summit GL benchmark was running, and a user working at
the physical display sees and reacts to every test capture; keep interactive
tests short.

## Unit tests

`make BUILD=build-host check-host` on Linux (or `make check` on Haiku) runs
`tests/HotKeyTests.cpp`: shortcut matching, labels and key names.

`make check-ui` on Haiku opens the launcher, settings and editor at 12- and
18-point fonts. It checks control bounds, visible settings sections, shortcut
recording cancellation, and an editor minimum width below 1000 pixels at the
normal font size. It does not save settings or capture the real screen.

## Capture startup timing

Run airShot with `AIRSHOT_TRACE=1` to log monotonic timestamps for the capture
request, hiding its windows, screen readback, window enumeration and showing
the overlay. Compare `requested` with `overlay shown` with the capture delay
set to zero. The 250 ms window-settle delay and the 150 ms cursor-hide delay
are intentional; a multi-second gap at `grabbing screen` is in screen readback.

`make capture-timing` builds a native probe using the same `GrabScreen` path.
Run `build-haiku/capture_timing` to measure eight captures, alternating cursor
inclusion, and validate their dimensions; the last two ask for screen pixels,
which on a scaled desktop come out larger by the scale (`scale=1.75` and
9935x2160 instead of 5677x1234 on the Framework 13 laptop at 175%, about
100 ms each). An optional limit in milliseconds
turns it into a performance regression check, for example
`build-haiku/capture_timing 1000` on X399. The limit is machine-dependent and
is deliberately separate from the portable unit tests.

Issue #1 was traced to `DrawingEngine::ReadBitmap` in X399's Haiku fork: at
200% display density it read the 7680x2160 GPU front buffer before downsampling
to 3840x1080. The fix belongs in Haiku's app_server: use the drawing buffer in
RAM at non-native density, where direct windows are disconnected and the
drawing buffer holds the complete desktop. At native density the front
buffer remains necessary to include direct-window rendering. Updating airShot
alone does not fix this OS bottleneck.

## Manual checklist

For icon changes, `make icon-dump` builds a native renderer/export probe.
Create an output directory and run `build-haiku/icon_dump <directory> <size>`
at 16, 20, 24, 32 and 48 pixels. It checks each icon has both visible pixels
and transparent space and exports all 19 as PNGs. Also inspect the installed
launcher and toolbar over VNC: antialiasing, the blur grid and the counter's
cut-out must survive button drawing. Confirm selected tools remain visible
and undo/redo visibly dim and enable as the document history changes.

1. Print Screen with nothing running: airShot starts and opens the editor
   with the full screen.
2. Shift+Print Screen: overlay in window mode; hovering moves the highlight
   with an animation; clicking captures the window (tab included, corners
   transparent); dragging switches to a region.
3. Ctrl+Print Screen: crosshair, drag a region, adjust with handles, Enter.
4. Editor: every tool, undo/redo, zoom (Alt+wheel), Save (notification and
   file in the configured folder), Copy (paste into another app).
5. Settings: record a new shortcut (the old one must not fire while
   recording), change the save folder, toggle the Deskbar icon.
6. Deskbar icon menu: captures, open, settings, quit.
7. `airShot picture.png` opens the file in the editor.
8. On a scaled display (Screen preferences, 150 or 175%): a full-screen
   capture is the screen's size in pixels (the settings show the scale), the
   overlay looks exactly like the live screen, its size label counts screen
   pixels, and the editor opens at 100% with sharp text; with *Scaled
   displays* at 100% the captures are the logical size again.

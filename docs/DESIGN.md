# airShot design notes

## Pieces

```
filter/airShotFilter.cpp   input_server add-on: watches key presses, sends
                           capture requests to the app (or launches it)
src/HotKey.*               shortcut model shared by app and filter (no Haiku
                           headers, unit tested on any platform)
src/Settings.*             flattened BMessage in ~/config/settings/airShot/settings,
                           read by both the app and the filter
src/App.*                  BApplication: capture orchestration, windows, Deskbar
src/capture/ScreenCapture  BScreen::GetBitmap or the screen's own pixels on a
                           scaled desktop, window list (private WindowInfo
                           API), cropping, transparent tab space
src/capture/OverlayWindow  full-screen picker over the frozen screen: window
                           hover highlight with animation, region selection
src/editor/Annotation      shape objects (arrow, line, rect, ellipse, pen,
                           highlighter, text, counter, blur) + blur filters
src/editor/CanvasView      zoomable canvas running the tools, undo/redo,
                           inline text editing
src/editor/EditorWindow    menus, BToolBar, colour swatches, save/copy
src/editor/Export          flatten to bitmap, PNG, clipboard, notifications
src/ui/MainWindow          launcher window
src/ui/SettingsWindow      settings; KeyCaptureControl records shortcuts
src/ui/DeskbarView         tray replicant (runs inside Deskbar's team)
```

## Capture flow

1. A request arrives (`kMsgCaptureFull/Window/Region`) from the filter, the
   Deskbar icon, the launcher or `ArgvReceived`. The app hides its own windows
   and waits 250 ms (plus the configured delay) so they are gone from the
   screen.
2. `ScreenCapture::GrabScreen` reads the whole screen. For a full-screen
   capture this is the result. On a scaled desktop (see below) it is in the
   screen's own pixels unless *Scaled displays* is set to 100%, and every
   rectangle in screen coordinates is mapped to them with
   `ScreenCapture::ToPixels` (`floor(x * scale)`, as app_server does).
3. Otherwise `ScreenCapture::ListWindows` collects the visible windows of the
   workspace (front to back, our team excluded, desktop last) and an
   `OverlayWindow` (a `kWindowScreenWindow`, above everything) shows the
   frozen screen dimmed to 62.5%. The user picks a window or a region; the
   overlay reports a screen rectangle plus window details.
4. The rectangle is cropped out of the frozen bitmap. For a decorated window
   the owning application is asked for the tab frame (scripting `TabFrame`)
   and the space beside the tab is made transparent, as Haiku's Screenshot
   does.
5. Depending on the settings the bitmap opens in an `EditorWindow`, goes to
   the clipboard, or is saved as PNG (with a notification).

## Scaled displays

air/OS draws the whole desktop at a render scale (100 to 250 percent) while
windows keep logical coordinates. `BScreen::GetBitmap()` averages the frame
buffer down to the logical size, which softens text at 125, 150 and 175
percent. For a capture in screen pixels `GrabScreen` shows a one pixel
`BDirectWindow` off the screen with `B_DIRECT_DEVICE_PIXELS` (and
`B_AVOID_FOCUS`, so nothing captured changes); app_server tells it the scale
and hands it its RAM copy of the whole desktop (`drawing_bits_area`), which
is cloned and copied (the technique of air/OS's `devshot` tool). That copy
never has the pointer in it, so "Include the mouse pointer" draws
`get_mouse_bitmap()` in where app_server would (system pointers are already
at the screen's density, others are enlarged the same way). Without the copy
the frame buffer is read, with the pointer hidden as for `GetBitmap()`.

The overlay and the editor show such a bitmap one to one with
`B_DEVICE_PIXEL_BITMAP`, so the frozen screen looks exactly like the live
one and the editor's actual size is pixel for pixel. That option came with
`BScreen::DeviceScale()`; airShot looks the function up at run time and
captures at 100% where it is missing. The flags and the `direct_buffer_info`
fields are used by value and offset, so airShot still builds with older
headers (the air/OS CI SDK) and on Haiku.

The editor takes the pixel scale along: actual size is a zoom of
`1 / scale`, the status bar shows zoom relative to it, and the style's
widths and font sizes are multiplied by it when annotations are made.

## The filter

`input_server` loads every add-on in `add-ons/input_server/filters`. Ours keeps
a `HotKeyDispatcher` looper that owns a copy of the settings, watches the
settings directory with `BPathMonitor`, and does the delivery, so
`Filter()` itself only compares key codes and never blocks the input thread.
A matching key press is swallowed (`B_SKIP_MESSAGE`), and so is its key-up.
Repeats (`be:key_repeat`) are ignored. While the settings window records a new
shortcut it sets `hotkeys_paused`, so the current shortcuts can be pressed
without firing.

When the app is not running, the filter launches it with `--full/--window/
--region`: command line arguments reach `ArgvReceived` before `ReadyToRun`,
which lets the app skip opening its launcher window.

## The overlay

The overlay keeps two bitmaps: the screen and a dimmed copy (one shift-and-
mask per pixel). Drawing paints the dimmed copy for the update rectangle, the
bright original inside the selection, then the frame (a breathing accent line
with corner brackets), handles, labels and the hint. The highlight rectangle
eases towards its target at every 16 ms tick, and only the changed strips are
invalidated, so even 4K screens animate smoothly.

## The editor

Annotations are objects drawn in image coordinates; the canvas uses
`SetOrigin`/`SetScale` so everything (pen sizes, fonts, bitmaps) follows the
zoom. Undo/redo stores deep copies of the annotation list (the base bitmap is
shared until a crop replaces it). Blur annotations cache a filtered copy of
the pixels under them; the cache is dropped when they move or the base
changes. Export draws the base and the annotations into an offscreen
`B_RGBA32` bitmap.

## Platform gotchas found on the X399 workstation (air/OS fork, 2026-10-01)

- **Offscreen bitmap alpha is undefined.** Drawing into a `B_RGBA32` bitmap
  that accepts views leaves the alpha channel of every touched pixel random,
  and `FillRect` in `B_OP_ALPHA` mode writes garbage colour too. Toolbar icons
  are therefore rendered twice (on white and on black, `B_RGB32`) and their
  alpha is recovered from the difference (`ToolIcons.cpp`); exports are
  flattened into opaque `B_RGB32` bitmaps (`Export::Flatten`). Rectangles in
  icons are drawn as polygons/polylines. `tests/IconProbe*.cpp` reproduce it.
- **input_server loads a *created* filter, not a renamed one.** Replacing
  the add-on with `mv` leaves nothing loaded; `tools/install-dev.sh` removes
  the old file and copies the new one. Package activation is fine.
- **Never overwrite the app binary in place** while Deskbar shows the tray
  icon: Deskbar maps the executable and crashes. Install a new file (rename)
  or a package.
- **Screen readback does not include the overlay** while a GL-compositing
  Summit benchmark runs; `BScreen::GetBitmap` and the VNC server both miss
  it although it is on the physical display. Trace the overlay with
  `AIRSHOT_TRACE=1` (stderr) instead of relying on VNC screenshots.
- A `B_POINTER_EVENTS` event mask with `B_NO_POINTER_HISTORY` set
  permanently on the overlay view stopped `MouseMoved()`; the view relies on
  being under the pointer and on `SetMouseEventMask()` during drags.
- `BBox` sizes itself from its first child view: give it a container view
  with the layout instead of setting a layout on the box itself.
- The physical screen is 7680×2160 with 2× scaling; `BScreen::GetBitmap`
  gives 3840×1080 logical pixels, the default screen-resolution capture
  7680×2160. The Framework 13 laptop (2026-10-18) runs its panel and an
  external monitor at 175%: 5677×1234 logical, 9935×2160 screen pixels.

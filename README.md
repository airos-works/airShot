# airShot

A screenshot and markup tool for air/OS and Haiku, in the spirit of Shottr and
Flameshot.

- **Three ways to capture**, each on its own keyboard shortcut that works while
  airShot runs in the background: the full screen, a window (highlighted with an
  animated frame as you point at it) or a region you drag out and adjust.
- **An editor for markup**: arrows, lines, rectangles, ellipses, a freehand
  pen, a highlighter, text, numbered markers, blur/pixelate and crop, with
  undo/redo, a colour palette and zoom.
- **Sharp on scaled displays**: with HiDPI or fractional scaling (125%, 175%…)
  captures keep every pixel of the screen by default instead of being
  averaged down to 100%; *Settings… ▸ Scaled displays* switches to 100%.
- **Fast output**: save as PNG into a folder of your choice with a time-stamped
  name, or copy to the clipboard; desktop notifications confirm both.
- **Haiku native**: an `input_server` add-on provides the global shortcuts, a
  Deskbar tray icon offers the capture modes, and the app also opens image
  files for annotation ("Open with…").

<!-- airos-ci:latest-builds:start -->
## Latest builds

Built automatically by air/OS CI from commit `d134e61` on 2026-10-06 ([all files](https://github.com/jmgasper/airShot/releases/tag/latest)).

| Architecture | Package |
|---|---|
| arm64 | [airshot-0.1.0.beta-6-arm64.hpkg](https://github.com/jmgasper/airShot/releases/download/latest/airshot-0.1.0.beta-6-arm64.hpkg) |
| x86_64 | [airshot-0.1.0.beta-6-x86_64.hpkg](https://github.com/jmgasper/airShot/releases/download/latest/airshot-0.1.0.beta-6-x86_64.hpkg) |

Install with `pkgman install <file>`, or copy the file into `/boot/system/packages`. Haiku SDK: x86_64 hrev60206-683-g3c1ce90a17, arm64 hrev60206-683-g3c1ce90a17.
<!-- airos-ci:latest-builds:end -->

## Default shortcuts

| Action        | Shortcut             |
|---------------|----------------------|
| Full screen   | `Print Screen`       |
| Window        | `Shift+Print Screen` |
| Region        | `Ctrl+Print Screen`  |

Change them in *Settings…* (click a shortcut, press the new keys; Backspace
clears it). The shortcuts replace Haiku's built-in Print Screen handling while
the add-on is installed.

## Using the overlay

- **Window mode**: move the mouse over a window and click. Dragging instead
  selects a region.
- **Region mode**: drag out a rectangle. Click a window without dragging to use
  its frame. Adjust with the handles, move by dragging inside, nudge with the
  arrow keys (Shift for 10 px), then press `Enter`, double-click, or click
  *Capture*. `Esc` cancels.

## The editor

| Key | Tool        | Key | Tool         |
|-----|-------------|-----|--------------|
| V   | Select/move | H   | Highlighter  |
| A   | Arrow       | T   | Text         |
| L   | Line        | N   | Counter      |
| R   | Rectangle   | B   | Blur         |
| E   | Ellipse     | C   | Crop         |
| P   | Pen         |     |              |

A capture in screen pixels opens at its actual size, pixel for pixel as it
was on the screen (100% in the status bar); annotation widths and font sizes
are in screen units, so they look the same at any display scale.

`Alt+S` saves (to the configured folder, or the file it was saved to before),
`Shift+Alt+S` saves elsewhere, `Alt+C` copies the image, `Alt+Z`/`Shift+Alt+Z`
undo/redo, `Delete` removes the selected annotation, `Alt` + mouse wheel zooms.
Text: `Enter` finishes, `Shift+Enter` starts a new line, `Esc` discards.

## Command line

```
airShot --full | --window | --region | --settings
airShot picture.png        # open an image in the editor
```

## Building

airShot builds on Haiku with the bundled development tools:

```
make            # build-haiku/airShot and the input_server filter
make package    # artifacts/airshot-<version>-x86_64.hpkg
```

Install the package with `pkgman install artifacts/airshot-*.hpkg`; the
`input_server` loads the shortcut filter as soon as the package is activated.
Bump the version in `resources/airShot.PackageInfo` before reinstalling.

On Linux, `make BUILD=build-host check-host` runs the platform independent
tests. `tools/sync-build.sh` syncs the tree to the test workstation and builds
there; `tools/install-dev.sh --run` installs the filter into
`~/config/non-packaged` and starts the app without a package.

See `docs/DESIGN.md` for how the pieces fit together and `docs/TESTING.md` for
the testing setup.

## Licence

MIT, see `LICENSE`.

Toolbar artwork includes adapted Font Awesome Free 6.7.2 icons by Fonticons,
Inc., licensed under CC BY 4.0. See [icon sources and attribution](resources/icons/README.md).

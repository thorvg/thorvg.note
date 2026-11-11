# ThorNote

<p align="center">
  <img width="550" height="auto" src="https://raw.githubusercontent.com/thorvg/thorvg.site/main/readme/logo/animated_brand.svg">
</p>

A native plain-text editor rendered with ThorVG. Includes multiple documents, configurable layouts, IME input and recovery of unsaved work.

## Build and run

Requires a C/C++20 toolchain, Meson 1.2+, Ninja, pkg-config, Python 3, ThorVG 1.2+ (CPU renderer and TTF loader) and SDL3 3.2+. ThorVG and SDL3 must be installed. Lua 5.4+ is used when available; otherwise Meson downloads it.

```sh
meson setup build
meson compile -C build
meson test -C build --print-errorlogs
build/src/thornote/ThorNote notes.txt
```

Use `build/` for all builds. ThorNote, its libraries and Lua support are always included. Desktop builds also include tests.

On Linux, `meson install -C build` installs ThorNote with its desktop launcher and icon.

On macOS, `meson install -C build` also installs `/Applications/ThorNote.app`, including its icon, fonts and layouts. The bundle is signed locally for running on the build machine; ThorVG and SDL3 must remain installed. The `ThorNote` and `tnote` commands are installed into the configured prefix as well.

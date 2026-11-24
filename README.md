[![LFX Active Contributors](https://insights.linuxfoundation.org/api/badge/active-contributors?project=thorvg&repos=https://github.com/thorvg/thorvg.note)](https://insights.linuxfoundation.org/project/thorvg/repository/thorvg_thorvg.note)
[![License](https://img.shields.io/badge/licence-MIT-green.svg?style=flat)](LICENSE)
[![Wikipedia](https://img.shields.io/badge/Wikipedia-000000?style=flat&logo=wikipedia&logoColor=white)](https://en.wikipedia.org/wiki/Thor_Vector_Graphics)
[![Discord](https://img.shields.io/badge/Community-5865f2?style=flat&logo=discord&logoColor=white)](https://discord.gg/n25xj6J6HM)
[![OpenCollective](https://img.shields.io/badge/OpenCollective-84B5FC?style=flat&logo=opencollective&logoColor=white)](https://opencollective.com/thorvg)

# ThorNote

<p align="center">
  <img width="550" height="auto" src="https://raw.githubusercontent.com/thorvg/thorvg.site/main/readme/logo/animated_brand.svg">
</p>

<h3 align="center">A lightweight, native text editor powered by ThorVG</h3>

ThorNote is designed for everyday writing and note-taking, combining a minimal interface with Markdown editing, flexible layouts, and animated emoji. It supports multiple documents, international text input (IME), and automatic recovery of unsaved work.

Built on [ThorVG](https://github.com/thorvg/thorvg), ThorNote demonstrates how ThorVG can power a complete native desktop application.

## Features

### Markdown Editing

Write and edit Markdown directly, with support for headings, emphasis, links, code, and interactive task lists.

<img width="700" src="docs/page/media/markdown-writing.gif" alt="Markdown editing">

### Motion Emoji

Bring your text to life with animated emoji. Type `:` followed by part of an emoji name and select a match.</br>
Emoji animate inline while remaining compatible with standard Unicode text.

<img width="700" src="docs/page/media/emoji-writing.gif" alt="Motion emoji">

### Layout Modes

Choose between two layouts to suit your workflow:

- **Memo** — Keep multiple documents organized in tabs.
- **Note** — Browse and organize documents using sidebars.

<img width="700" src="docs/page/media/layout-switching.gif" alt="Flexible layouts">

### Light & Dark Themes

Switch seamlessly between light and dark themes through the command palette.

<img width="700" src="docs/page/media/theme-switching.gif" alt="Light and dark themes">

### Additional Features

- **Multi-document Editing** — Work with multiple documents within a single application.
- **Command Palette** — Quickly access commands, themes, and layout settings.
- **International Text Input** — Write in multiple languages with IME support.
- **Session Recovery** — Recover unsaved work.

## Build & Installation

### Requirements

- C/C++ toolchain with C++20 support
- Meson 1.2+
- Ninja
- pkg-config
- Python 3
- [ThorVG](https://github.com/thorvg/thorvg) 1.2+ (CPU and GL renderers, and TTF loader)
- [SDL3](https://github.com/libsdl-org/SDL) 3.2+
- Lua 5.4+ (automatically downloaded if unavailable)

ThorVG and SDL3 must be installed before building ThorNote.

### Build and Run

```sh
meson setup build
meson compile -C build
build/src/thornote/ThorNote notes.txt
```

#### Linux

Installs ThorNote along with its desktop launcher and application icon.

#### macOS

Installs `/Applications/ThorNote.app`, including its application icon, fonts, and layouts. The application bundle is locally signed for execution on the build machine.

ThorVG and SDL3 must remain installed. The `ThorNote` and `tnote` commands are also installed into the configured prefix.

## License

### Third-party Assets

The [Animated Noto Emoji](https://googlefonts.github.io/noto-emoji-animation/) assets are copyright Google LLC and licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

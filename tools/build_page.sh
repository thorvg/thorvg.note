#!/bin/sh
# Builds the web editor into docs/page twice: with threads for cross-origin isolated pages and without for hosts that
# cannot send the isolation headers. Run it with the Emscripten environment active.
set -e

root=$(cd "$(dirname "$0")/.." && pwd)
out="$root/build/page"
command -v emcc >/dev/null || { echo "emcc is not found; activate the Emscripten environment first" >&2; exit 1; }

build()
{
    if [ ! -d "$out/$1" ]; then
        meson setup "$out/$1" "$root" --cross-file "$root/cross/wasm32.txt" -Dthreads="$2"
    fi
    ninja -C "$out/$1"
}

build threads true
build single false
cp -v "$out/threads/src/thornote/ThorNote.threads.js" "$out/threads/src/thornote/ThorNote.threads.wasm" \
    "$out/single/src/thornote/ThorNote.js" "$out/single/src/thornote/ThorNote.wasm" \
    "$out/single/src/thornote/ThorNote.recovery.js" "$root/docs/page/"

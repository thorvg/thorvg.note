import os
from pathlib import Path
import subprocess
import sys
import tempfile


executable = sys.argv[1]
environment = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")


def run(*arguments, expected=0):
    result = subprocess.run(
        [executable, *map(str, arguments)],
        env=environment,
        capture_output=True,
        text=True,
        timeout=25,
    )
    if result.returncode != expected:
        raise RuntimeError(
            f"{arguments}: exit {result.returncode}, expected {expected}\n"
            f"{result.stdout}{result.stderr}"
        )
    return result


assert "usage:" in run("--help").stdout
run("--invalid-option", expected=2)

with tempfile.TemporaryDirectory(prefix="thornote-smoke-") as directory:
    root = Path(directory)
    note = root / "sample.md"
    note.write_text("# Sample\n\nText rendering and file loading.\n", encoding="utf-8")
    cases = {
        "welcome": [],
        "memo_folder_session": ["--layout", "memo"],
        "memo": ["--layout", "memo", note],
        "note": ["--layout", "note", root],
        "settings": ["--settings"],
    }
    for name, arguments in cases.items():
        image = root / f"{name}.ppm"
        settings = root / name
        settings.mkdir()
        restored_folder = name == "memo_folder_session"
        preferences = "thornote-settings 1\ndark 0\n"
        if restored_folder:
            preferences += f'recent-directory "{root}"\n'
            (settings / "session").mkdir()
            (settings / "session/session.conf").write_text(
                'thornote-session 1\nactive 0\nwindow 1080 720\nsettings 0 0 0\nnotes 0 "all" ""\nfolders 1\n',
                encoding="utf-8",
            )
        (settings / "settings.conf").write_text(preferences, encoding="utf-8")
        if name in ("welcome", "memo_folder_session"):
            (settings / "themes").mkdir()
            (settings / "themes/light.lua").write_text(
                'return {schema = 1, colors = {text = "#6D28D9"}}\n', encoding="utf-8"
            )
        run(*([] if restored_folder else ["--fresh"]), "--settings-dir", settings, "--snapshot", image, *arguments)
        with image.open("rb") as stream:
            assert stream.readline() == b"P6\n", name
            width, height = map(int, stream.readline().split())
            assert width > 0 and height > 0, name
            assert stream.readline() == b"255\n", name
            pixels = stream.read()
        assert len(pixels) == width * height * 3, name
        assert len(set(pixels)) > 1, name
        if name in ("welcome", "memo_folder_session"):
            assert pixels[:width * (height // 3) * 3].count(bytes.fromhex("6D28D9")) > 1000, name
        print(f"{name}: rendered {width}x{height}")

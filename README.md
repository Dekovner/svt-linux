# svt-linux

A native 1:1 Linux port of **[jmagine/svt-rs](https://github.com/jmagine/svt-rs/releases/tag/v1.0.3)** (`v1.0.3`) built with **C++20 + GTK3**.

Runs natively on Linux without Wine while preserving the exact `f32` math, `300x350` UI layout, all 24 tooltips, `.osu` file Drag-and-Drop, and `svt_config.txt` compatibility.

## Features
- **Interpolation modes**: `Lin. SV`, `Exp. SV`, `Pol. SV`, `Sin. SV`, `Flat SV` (`Mult. Scaling`), and `Lin. Vol` (including the `v1.0.3` fix for `Flat SV` BPM scaling across uninherited timing lines).
- **Target objects**: `Hits`, `Snaps` (custom `X / Y Snapping` divisor + 25% extra snappings past the last object for fade-outs), and `Inh. lines`.
- **Advanced options**: `Offset`, `Buffer`, `Min. Spacing`, `Ignore BPM`, `Preview Diff` (`[preview].osu`), automatic backup (`backup.osu`), and instant `Undo` via atomic file swap (`renameat2` / `RENAME_EXCHANGE`).
- **XDG Config Support**: Stores `svt_config.txt` and `backup.osu` in `~/.config/svt/` when launched from an application menu (or uses `./svt_config.txt` if present in the working directory).

## Quick Install (with Application Menu / `.desktop` entry)
Run this one-liner to clone the repository and install `SVT` into `~/.local/bin/svt` along with its icon and `.desktop` launcher entry (for Rofi, Wofi, KDE, GNOME, etc.):

```bash
git clone https://github.com/Dekovner/svt-linux.git && cd svt-linux && ./install.sh
```

To uninstall:
```bash
make uninstall
```

## Run Prebuilt Binary Directly (Portable)
```bash
chmod +x ./svt
./svt
```

## Build from Source
Requires `g++` (with C++20 support) and `gtk+-3.0` development headers:
```bash
make
make install
```

## License
Distributed under the [MIT License](LICENSE). Original Windows project: [jmagine/svt-rs](https://github.com/jmagine/svt-rs) (Copyright (c) 2021 Jason Ma).

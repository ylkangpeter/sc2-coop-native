# StarCraft II Co-op Native (SC2 Coop Overlay)

[![Release](https://img.shields.io/github/v/release/ylkangpeter/sc2-coop-native?color=blue&style=flat-square)](https://github.com/ylkangpeter/sc2-coop-native/releases)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-0078d7?style=flat-square&logo=windows)](https://github.com/ylkangpeter/sc2-coop-native)
[![Language](https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square&logo=c%2B%2B)](https://en.wikipedia.org/wiki/C%2B%2B20)
[![DirectX](https://img.shields.io/badge/DirectX-11-red?style=flat-square)](https://docs.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-graphics)
[![License](https://img.shields.io/badge/license-MIT-green?style=flat-square)](LICENSE)

A ultra-fast, modern, lightweight, native Windows overlay and match statistics analyzer for **StarCraft II Co-op Missions**, written from the ground up in modern **C++20**, **DirectX 11**, and **Dear ImGui**.

Inspired by and redesigning the classic [FluffyMaguro/SC2_Coop_Overlay](https://github.com/FluffyMaguro/SC2_Coop_Overlay) by [Maguro](https://github.com/FluffyMaguro).

---

## 🌟 Background & Philosophy

The original [SC2_Coop_Overlay](https://github.com/FluffyMaguro/SC2_Coop_Overlay) created by community icon Maguro is a legendary tool beloved by StarCraft II Co-op commanders worldwide. Built on Python 3, PyQt5, and PyQtWebEngine (Chromium), it proved how much depth and joy post-match analytics and in-game ally reconnaissance bring to the Co-op experience.

However, running a full web runtime inside an overlay poses noticeable drawbacks:
- **Heft**: Distribution size over 400 MB with thousands of files.
- **Resource Footprint**: High memory usage (often hundreds of megabytes of RAM) and non-negligible CPU overhead while StarCraft II is running.
- **Window Management & Latency**: WebEngine transparent windows can suffer from click-through friction, multi-monitor tearing, and replay parsing delays.

**StarCraft II Co-op Native** is a clean-room, full rewrite into **pure native C++20 and Direct3D 11**:
- **Single Portable Binary (~11 MB)**: No Python runtime, no Qt libraries, no Chromium engine, zero setup or installer.
- **Minimal System Overhead**: Uses ~30–40 MB of RAM and ~0% idle CPU.
- **Blazing-Fast Replay Engine**: Direct MPQ archive decompression via StormLib and in-memory bit-level `s2protocol` decoding. Replays are parsed in milliseconds.
- **Flawless Native Win32 / DX11 HUD**: Click-through, borderless, hardware-composited layered overlay matching StarCraft II's authentic Eurostile UI aesthetics.

---

## ✨ Features

### 1. In-Game Ally Toast (Reconnaissance)
- Automatically triggers upon loading into a match.
- Displays your ally's commander, prestige, level, mutator experience, historical win rate, and custom player notes.
- Configurable display duration and auto-dismissal.

### 2. Post-Game Match Summary Overlay
- Pops up smoothly when a match concludes with configurable slide-in/slide-out animations.
- Borderless, transparent, non-intrusive click-through HUD that seamlessly overlays StarCraft II.
- Full match statistics:
  - Game duration, difficulty, map, victory/defeat status.
  - Player slots stay in replay order. The HUD includes mutation icons, player contributions, allocated masteries and a per-unit kill breakdown.
  - Bonus counts use map objective events. Missing tracker samples are shown as unavailable.
  - Army value, supply, kills and resource collection charts can be toggled in Settings, along with total kills, the session record and a randomized commander/prestige suggestion.

### 3. Match History & Replay Analyzer (`Games`)
- Automatically monitors StarCraft II Accounts replay directories in real-time.
- Displays chronological match history with win/loss indicators, commanders, maps, and dates.
- **Detailed Match Modal**: View unit kills, masteries, mutators, and APM curves.
- **In-Game Chat Log Viewer**: Decodes in-game communication (Allies/All chat) with timestamps.

### 4. Player Database & Notes (`Players`)
- Track everyone you've played with across the Koprulu sector.
- Add personal player notes, review joint match win rates, and inspect commander affinities.
- Real-time search and sorting by games played, win rate, or player tag.

### 5. Mutations & Weekly Challenges (`Weeklies`)
- Archive of StarCraft II weekly mutations.
- Check past weekly mutations, individual mutator descriptions, and community challenge records.

### 6. Aggregated Statistics & Analytics (`Stats`)
- Commander tier list and performance charts (Win rates, Pick rates, average game durations).
- Mutator impact analysis.
- Filterable by game mode, difficulty (Casual to Brutal+6), and time periods.

### 7. Rich Customization (`Settings`)
- Customizable Global Hotkey (Default: `F11`) to summon the full management dashboard.
- Chat Modal Hotkey (Default: `Ctrl + C`).
- Adjustable overlay transparency, animation speed, and screen positioning.
- Automatic account replay folder detection with manual override support.

---

## 🚀 Performance Comparison

| Metric | Original Python Overlay | **SC2 Co-op Native** |
| :--- | :--- | :--- |
| **Language & Runtime** | Python 3 + PyQt5 + PyQtWebEngine | **Modern C++20 + Direct3D 11** |
| **Package Size** | ~400 MB+ (Extracted) | **~11.8 MB** (Single portable executable) |
| **Memory Footprint (RAM)** | ~250 MB – 500 MB | **~35 MB – 45 MB** |
| **Startup Time** | ~3 – 6 seconds | **Instant (< 150 ms)** |
| **Replay Parse Time** | ~1 – 2 seconds | **~10 – 30 ms** |
| **In-Game Overlay** | Web page rendered in window | **DirectX 11 hardware-accelerated click-through** |

---

## 📥 Download & Usage

1. Go to the [**Releases**](https://github.com/ylkangpeter/sc2-coop-native/releases) page and download `sc2-coop-native-v1.0.24-windows-x64.zip`.
2. Extract the archive anywhere on your PC (e.g. `D:\Tools\SC2_Coop_Native\`).
3. Run `sc2_coop.exe`.
4. Launch **StarCraft II** in **Windowed (Fullscreen)** or **Windowed** mode.
5. The overlay will automatically detect newly saved replays and display ally toasts and post-game summaries!
   - Press **`F11`** to open or close the main Dashboard.

---

## 🛠️ Building from Source

Requirements: Windows 10/11, built-in Windows PowerShell 5.1+, and LLVM-MinGW (UCRT, x64). Python is not required for building, tests, packaging, or running the application.

```powershell
winget install MartinStorsjo.LLVM-MinGW.UCRT
.\build.ps1
.\build.ps1 -Test
.\build.ps1 -Test -TestFilter test_replay_notifications
.\build.ps1 -Package
```

To include the optional private replay audit in the test run, set `$env:SC2_AUDIT_REPLAY_DIR` to the directory containing the named audit recordings. The audit reports missing files individually and fails if a present recording cannot be parsed or its statistics differ from the expected values.

`build.bat` and `package.bat` invoke the same PowerShell entry point. If local script execution is restricted, invoke `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Package` (this applies only to that process). Use `-CompilerBin` for a custom LLVM-MinGW location, `-Clean` to rebuild dependencies, or `-OutputDirectory dist/sc2_coop_v1.0.18` to build alongside a running version. The build never kills the running app.

Packaging includes only the executable and asset files, with a SHA-256 checksum beside the ZIP. Databases, personal settings, logs, dumps and old executables are excluded. `-PackageOnly` packages an existing build.

### Incremental replay discovery

Startup and explicit scans read their last discovery watermark from the local SQLite database; an existing installation without a watermark uses its latest indexed replay timestamp, while an empty database returns zero. The same timestamp filter handles all cases. Replay creation time is included so files copied in with preserved modification times can be found. Equal timestamps are included and file version markers prevent duplicate parsing.

Changed paths are durably queued in the same transaction that advances the discovery watermark. Interruptions, active writers and failed database saves leave work queued for the next run. During normal operation, Windows `ReadDirectoryChangesW` supplies changed paths directly: idle ticks do not enumerate the library or reload whole-library indexes. New or renamed directories are checked for contained replays. A notification overflow or unavailable watch falls back to timestamp-based catch-up; the watermark is conservatively advanced only after a complete enumeration, never merely because a notification arrived. “Re-parse All Replays” enumerates the whole folder and queues a durable request to rebuild each file.

Historical matches appear in the list as soon as their summaries are saved. The header then shows **Analyzing details** while up to four workers calculate unit statistics and refine match durations. Closing the app during either stage leaves unfinished files queued for the next launch. Recent matches still receive full analysis for their post-game popups.

**Pending retry** means some work could not be saved or a replay could not be read. Check that the replay folder is accessible and the database drive has free space. Settings shows the count of files that failed to parse and offers **Re-parse All Replays** to rebuild from the original files.

### Keyboard and overlay overhead

Global shortcuts use `RegisterHotKey`; no low-level keyboard hook remains installed while replay overlays are closed and shortcut recording is inactive. Recording and visible overlays temporarily enable the hook for capture, Escape and PageUp/PageDown controls. Ordinary game/typing keys bypass modifier queries. Recording suspends global shortcut actions and cancels when the app loses focus; unavailable shortcuts are identified in Settings.

Summary slide animations preserve Z order. Stable display performs no frame-by-frame repositioning or topmost updates. An asynchronous foreground-change notification restores ordering when switching applications, with a once-per-second recovery check that only changes Z order when topmost status is lost or StarCraft II has overtaken the overlay. These hooks are removed when the summary closes.


## 🧩 Architecture

```text
SC2_Coop_Native/
├── src/
│   ├── app_state.cpp / .h       # Global application state & event bus
│   ├── db/                      # SQLite3 embedded storage layer
│   ├── replay/                  # StormLib MPQ unpacker & s2protocol parser
│   ├── ui/                      # DirectX 11 backend & ImGui manager
│   │   ├── hud/                 # In-game toast & post-match summary overlays
│   │   ├── tabs/                # Games, Players, Weeklies, Stats, Settings
│   │   └── modals/              # Match detail modal & chat log viewer
│   └── utils/                   # Win32 click-through, hotkeys, process utils
├── vendor/                      # ImGui, ImPlot, StormLib, SQLite3, STB, nlohmann-json
├── assets/                      # Eurostile font, commander portraits, map icons
└── res/                         # Windows application icon & resource script
```

---

## 📜 Credits & Acknowledgments

- **[FluffyMaguro (Maguro)](https://github.com/FluffyMaguro)**: Author of [SC2_Coop_Overlay](https://github.com/FluffyMaguro/SC2_Coop_Overlay) and [Maguro Maps](https://maguro.one/). His years of dedication to StarCraft II Co-op data extraction, replay structures, and mutator logic made this project possible.
- **[Blizzard Entertainment](https://starcraft2.blizzard.com/)**: For creating StarCraft II and its cooperative mission commanders.
- **[StormLib](https://github.com/ladislav-zezula/StormLib)** by Ladislav Zezula: The definitive library for Blizzard MPQ archives.
- **[Dear ImGui](https://github.com/ocornut/imgui)** by Omar Cornut & **[ImPlot](https://github.com/epezent/implot)** by Evan Pezent.

---

## 📄 License

Distributed under the [MIT License](LICENSE).

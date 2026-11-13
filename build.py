#!/usr/bin/env python3
import os
import sys
import shutil
import subprocess
from pathlib import Path

import glob

PROJECT_ROOT = Path(__file__).parent.resolve()

def find_llvm_bin() -> Path:
    # 1. Check PATH
    which_clang = shutil.which("clang++") or shutil.which("clang++.exe")
    if which_clang:
        return Path(which_clang).parent

    # 2. Check WinGet LLVM-MinGW packages
    if os.name == 'nt':
        local_appdata = os.environ.get("LOCALAPPDATA", "")
        if local_appdata:
            matches = list(Path(local_appdata).glob("Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW*/**/bin/clang++.exe"))
            if matches:
                return matches[0].parent
    else:
        matches = glob.glob("/mnt/c/Users/*/AppData/Local/Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW*/**/bin/clang++.exe", recursive=True)
        if matches:
            return Path(matches[0]).parent

    return Path("/usr/bin")

LLVM_BIN = find_llvm_bin()

CLANG = LLVM_BIN / "clang.exe"
CLANG_PP = LLVM_BIN / "clang++.exe"
WINDRES = LLVM_BIN / "x86_64-w64-mingw32-windres.exe"
BUILD_DIR = PROJECT_ROOT / "build"
DIST_DIR = PROJECT_ROOT / "dist" / "sc2_coop"

CXX_FLAGS = [
    "-std=c++20",
    "-O2",
    "-DUNICODE",
    "-D_UNICODE",
    "-DWIN32_LEAN_AND_MEAN",
    "-DNOMINMAX",
    "-Isrc",
    "-Ivendor",
    "-Ivendor/imgui",
    "-Ivendor/implot",
    "-Ivendor/nlohmann",
    "-Ivendor/stb",
    "-Ivendor/sqlite3",
    "-Ivendor/stormlib",
]

SOURCES = [
    PROJECT_ROOT / "src" / "main.cpp",
    PROJECT_ROOT / "src" / "app_state.cpp",
    PROJECT_ROOT / "src" / "i18n" / "i18n.cpp",
    PROJECT_ROOT / "src" / "db" / "database.cpp",
    PROJECT_ROOT / "src" / "replay" / "mpq_reader.cpp",
    PROJECT_ROOT / "src" / "replay" / "s2_decoder.cpp",
    PROJECT_ROOT / "src" / "replay" / "replay_parser.cpp",
    PROJECT_ROOT / "src" / "replay" / "replay_watcher.cpp",
    PROJECT_ROOT / "src" / "utils" / "win32_utils.cpp",
    PROJECT_ROOT / "src" / "utils" / "hotkey_manager.cpp",
    PROJECT_ROOT / "src" / "ui" / "texture_loader.cpp",
    PROJECT_ROOT / "src" / "ui" / "ui_manager.cpp",
    PROJECT_ROOT / "src" / "ui" / "hud" / "summary_overlay.cpp",
    PROJECT_ROOT / "src" / "ui" / "hud" / "ally_toast.cpp",
    PROJECT_ROOT / "src" / "ui" / "tabs" / "tab_settings.cpp",
    PROJECT_ROOT / "src" / "ui" / "tabs" / "tab_games.cpp",
    PROJECT_ROOT / "src" / "ui" / "tabs" / "tab_players.cpp",
    PROJECT_ROOT / "src" / "ui" / "tabs" / "tab_weeklies.cpp",
    PROJECT_ROOT / "src" / "ui" / "tabs" / "tab_stats.cpp",
    PROJECT_ROOT / "src" / "ui" / "modals" / "modal_game_detail.cpp",
    PROJECT_ROOT / "src" / "ui" / "modals" / "modal_chat_view.cpp",
    PROJECT_ROOT / "vendor" / "imgui" / "imgui.cpp",
    PROJECT_ROOT / "vendor" / "imgui" / "imgui_draw.cpp",
    PROJECT_ROOT / "vendor" / "imgui" / "imgui_tables.cpp",
    PROJECT_ROOT / "vendor" / "imgui" / "imgui_widgets.cpp",
    PROJECT_ROOT / "vendor" / "imgui" / "backends" / "imgui_impl_win32.cpp",
    PROJECT_ROOT / "vendor" / "imgui" / "backends" / "imgui_impl_dx11.cpp",
    PROJECT_ROOT / "vendor" / "implot" / "implot.cpp",
    PROJECT_ROOT / "vendor" / "implot" / "implot_items.cpp",
]

def get_max_header_mtime():
    mtime = 0.0
    for p in (PROJECT_ROOT / "src").rglob("*.h"):
        mtime = max(mtime, p.stat().st_mtime)
    return mtime

MAX_HEADER_MTIME = get_max_header_mtime()

def compile_file(src_path: Path, obj_path: Path):
    if obj_path.exists() and obj_path.stat().st_mtime >= src_path.stat().st_mtime:
        return
    obj_path.parent.mkdir(parents=True, exist_ok=True)
    rel_src = src_path.relative_to(PROJECT_ROOT)
    rel_obj = obj_path.relative_to(PROJECT_ROOT)
    print(f"Compiling {rel_src}...")
    cmd = [str(CLANG_PP)] + CXX_FLAGS + ["-c", str(rel_src), "-o", str(rel_obj)]
    res = None
    for attempt in range(12):
        res = subprocess.run(cmd, cwd=PROJECT_ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode == 0:
            break
        if "UtilAcceptVsock" in res.stderr or "vsock" in res.stderr or res.returncode == 1:
            import time
            time.sleep(1.0)
            continue
        print(f"FAILED to compile {src_path}:\n{res.stderr}")
        sys.exit(1)
    if res and res.returncode != 0:
        print(f"FAILED to compile {src_path}:\n{res.stderr}")
        sys.exit(1)

def build_resources(rc_path: Path, res_obj: Path):
    if res_obj.exists() and res_obj.stat().st_mtime >= rc_path.stat().st_mtime:
        return
    res_obj.parent.mkdir(parents=True, exist_ok=True)
    rel_rc = rc_path.relative_to(PROJECT_ROOT)
    rel_obj = res_obj.relative_to(PROJECT_ROOT)
    print(f"Building resource {rel_rc}...")
    cmd = [str(WINDRES), str(rel_rc), "-O", "coff", "-o", str(rel_obj)]
    res = subprocess.run(cmd, cwd=PROJECT_ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    if res.returncode != 0:
        print(f"FAILED to build resource {rc_path}:\n{res.stderr}")
        sys.exit(1)

def main():
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    DIST_DIR.mkdir(parents=True, exist_ok=True)

    # 1. Compile resource
    rc_file = PROJECT_ROOT / "res" / "resource.rc"
    res_obj = BUILD_DIR / "resource.o"
    build_resources(rc_file, res_obj)

    # 2. Compile C++ sources
    obj_files = []
    for src in SOURCES:
        rel = src.relative_to(PROJECT_ROOT)
        obj_name = str(rel).replace("/", "_").replace("\\", "_") + ".o"
        obj_path = BUILD_DIR / obj_name
        compile_file(src, obj_path)
        obj_files.append(obj_path)

    # 3. Vendor dependencies
    sqlite_c = PROJECT_ROOT / "vendor" / "sqlite3" / "sqlite3.c"
    sqlite_obj = BUILD_DIR / "sqlite3.o"
    if not sqlite_obj.exists() or sqlite_obj.stat().st_mtime < sqlite_c.stat().st_mtime:
        print("Compiling sqlite3.c...")
        cmd = [str(CLANG), "-O2", "-c", str(sqlite_c.relative_to(PROJECT_ROOT)), "-o", str(sqlite_obj.relative_to(PROJECT_ROOT))]
        res = subprocess.run(cmd, cwd=PROJECT_ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode != 0:
            print(f"FAILED to compile sqlite3.c:\n{res.stderr}")
            sys.exit(1)

    stormlib_lib = PROJECT_ROOT / "vendor" / "stormlib" / "libstorm.a"
    if not stormlib_lib.exists():
        print("Building StormLib archive...")
        cmd = [sys.executable, "build_stormlib.py"]
        res = subprocess.run(cmd, cwd=PROJECT_ROOT / "vendor" / "stormlib", stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode != 0:
            print(f"FAILED to build StormLib:\n{res.stderr}")
            sys.exit(1)

    # 4. Link executable
    target_exe = DIST_DIR / "sc2_coop.exe"
    print(f"Linking {target_exe.name}...")
    # Write response file for linker to keep command line minimal
    rsp_file = BUILD_DIR / "link_args.rsp"
    rsp_args = (
        ["-static", "-mwindows"]
        + [str(o.relative_to(PROJECT_ROOT)) for o in obj_files]
        + [
            str(res_obj.relative_to(PROJECT_ROOT)),
            str(sqlite_obj.relative_to(PROJECT_ROOT)),
            str(stormlib_lib.relative_to(PROJECT_ROOT)),
        ]
        + [
            "-ld3d11",
            "-ld3dcompiler",
            "-ldwmapi",
            "-lole32",
            "-lwindowscodecs",
            "-lwininet",
            "-limm32",
            "-lgdi32",
            "-lshell32",
            "-luuid",
            "-o",
            str(target_exe.relative_to(PROJECT_ROOT)),
        ]
    )
    with open(rsp_file, "w") as rf:
        rf.write("\n".join(rsp_args))

    link_cmd = [str(CLANG_PP), f"@{rsp_file.relative_to(PROJECT_ROOT)}"]

    res = None
    for attempt in range(12):
        res = subprocess.run(link_cmd, cwd=PROJECT_ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode == 0:
            break
        if "UtilAcceptVsock" in res.stderr or "vsock" in res.stderr or res.returncode == 1:
            import time
            time.sleep(1.0)
            continue
        print(f"FAILED to link executable:\n{res.stderr}")
        sys.exit(1)
    if res and res.returncode != 0:
        print(f"FAILED to link executable:\n{res.stderr}")
        sys.exit(1)
    print(f"Successfully linked {target_exe} ({target_exe.stat().st_size // 1024} KB)")

    # 5. Copy assets
    dist_assets = DIST_DIR / "assets"
    src_assets = PROJECT_ROOT / "assets"
    if src_assets.exists():
        if dist_assets.exists():
            shutil.rmtree(dist_assets)
        shutil.copytree(src_assets, dist_assets)
        print(f"Copied assets to {dist_assets}")

    print("BUILD COMPLETE!")

if __name__ == "__main__":
    main()

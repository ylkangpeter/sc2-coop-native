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

def find_tool(name: str) -> Path:
    candidates = [
        LLVM_BIN / f"{name}.exe",
        LLVM_BIN / name,
    ]
    for c in candidates:
        if c.exists():
            return c
    return LLVM_BIN / (f"{name}.exe" if os.name == 'nt' or (LLVM_BIN / f"{name}.exe").exists() else name)

CLANG = find_tool("clang")
CLANG_PP = find_tool("clang++")
WINDRES = find_tool("x86_64-w64-mingw32-windres")
if not WINDRES.exists():
    WINDRES = find_tool("llvm-windres")
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
    PROJECT_ROOT / "src" / "ui" / "modals" / "modal_player_note.cpp",
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
    is_src = (PROJECT_ROOT / "src") in src_path.parents or src_path.parent == PROJECT_ROOT / "src"
    if obj_path.exists() and obj_path.stat().st_mtime >= src_path.stat().st_mtime:
        if not is_src or obj_path.stat().st_mtime >= MAX_HEADER_MTIME:
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
        if "UtilAcceptVsock" in res.stderr or "vsock" in res.stderr:
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
    res = None
    for attempt in range(12):
        res = subprocess.run(cmd, cwd=PROJECT_ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode == 0:
            break
        if "UtilAcceptVsock" in res.stderr or "vsock" in res.stderr:
            import time
            time.sleep(1.0)
            continue
        print(f"FAILED to build resource {rc_path}:\n{res.stderr}")
        sys.exit(1)
    if res and res.returncode != 0:
        print(f"FAILED to build resource {rc_path}:\n{res.stderr}")
        sys.exit(1)

def kill_running_app():
    try:
        if os.name == 'nt':
            subprocess.run(["powershell.exe", "-NoProfile", "-Command", "Stop-Process -Name sc2_coop* -Force -ErrorAction SilentlyContinue"], stdin=subprocess.DEVNULL, capture_output=True, timeout=5)
        else:
            ps_path = "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
            if os.path.exists(ps_path):
                subprocess.run([ps_path, "-NoProfile", "-Command", "Stop-Process -Name sc2_coop* -Force -ErrorAction SilentlyContinue"], cwd="/mnt/c", stdin=subprocess.DEVNULL, capture_output=True, timeout=5)
    except Exception:
        pass

def main():
    if "--package-only" in sys.argv:
        package_release()
        return

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
        res = None
        for attempt in range(12):
            res = subprocess.run(cmd, cwd=PROJECT_ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            if res.returncode == 0:
                break
            if "UtilAcceptVsock" in res.stderr or "vsock" in res.stderr:
                import time
                time.sleep(1.0)
                continue
            print(f"FAILED to compile sqlite3.c:\n{res.stderr}")
            sys.exit(1)
        if res and res.returncode != 0:
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

    # Pre-link: terminate running app so file locks and single-instance mutex are freed
    kill_running_app()
    if target_exe.exists():
        try:
            target_exe.unlink()
        except OSError:
            try:
                old_exe = target_exe.with_name(f"{target_exe.stem}_old{target_exe.suffix}")
                if old_exe.exists():
                    try:
                        old_exe.unlink()
                    except OSError:
                        pass
                target_exe.rename(old_exe)
            except OSError:
                pass

    link_cmd = [str(CLANG_PP), f"@{rsp_file.relative_to(PROJECT_ROOT)}"]

    res = None
    for attempt in range(12):
        res = subprocess.run(link_cmd, cwd=PROJECT_ROOT, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode == 0:
            break
        if "UtilAcceptVsock" in res.stderr or "vsock" in res.stderr:
            import time
            time.sleep(1.0)
            continue
        if "Permission denied" in res.stderr:
            print(f"\n[ERROR] Cannot overwrite {target_exe} because it is currently running in Windows!")
            print("Please close sc2_coop.exe on Windows before rebuilding.\n")
            sys.exit(1)
        print(f"FAILED to link executable:\n{res.stderr}")
        sys.exit(1)
    if res and res.returncode != 0:
        if "Permission denied" in res.stderr:
            print(f"\n[ERROR] Cannot overwrite {target_exe} because it is currently running in Windows!")
            print("Please close sc2_coop.exe on Windows before rebuilding.\n")
            sys.exit(1)
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

    # 6. Automatic packaging if requested via --package or -p
    if "--package" in sys.argv or "-p" in sys.argv:
        package_release()

def package_release(version: str = "v1.0.5"):
    import zipfile
    zip_name = f"sc2-coop-native-{version}-windows-x64.zip"
    zip_dest = PROJECT_ROOT / "dist" / zip_name
    print(f"\n========================================================")
    print(f" Packaging clean release archive: {zip_name}")
    print(f"========================================================")

    # Strict exclusions to prevent personal data, local settings, or temp files from leaking into releases
    EXCLUDE_EXTS = {".db", ".db-wal", ".db-shm", ".db-journal", ".ini", ".log", ".txt", ".tmp", ".bak"}
    EXCLUDE_PATTERNS = {"*old.exe", "crash_log*"}

    temp_zip = PROJECT_ROOT / "dist" / f"temp_{zip_name}"
    archived_count = 0
    excluded_count = 0

    with zipfile.ZipFile(temp_zip, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for root, _, files in os.walk(DIST_DIR):
            for f in sorted(files):
                p = Path(root) / f
                
                # Check exclusions
                if p.suffix.lower() in EXCLUDE_EXTS or any(p.match(pat) for pat in EXCLUDE_PATTERNS):
                    print(f"  [SHIELDED/EXCLUDED] {p.name}")
                    excluded_count += 1
                    continue
                
                rel_path = p.relative_to(PROJECT_ROOT / "dist")
                zf.write(p, rel_path.as_posix())
                archived_count += 1

    if zip_dest.exists():
        zip_dest.unlink()
    temp_zip.rename(zip_dest)

    print(f"\n[PACKAGE SUCCESS] {archived_count} files archived ({zip_dest.stat().st_size / (1024 * 1024):.2f} MB)")
    if excluded_count > 0:
        print(f"[SECURITY SHIELD] Blocked {excluded_count} sensitive/temporary runtime files from release.")
    print(f"Archive created at: {zip_dest}")

if __name__ == "__main__":
    main()

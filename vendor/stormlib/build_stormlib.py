import os
import sys
import subprocess
import shutil
import glob
from pathlib import Path

root = os.path.dirname(os.path.abspath(__file__))
os.chdir(root)

def find_bin_dir():
    which_clang = shutil.which("clang") or shutil.which("clang.exe")
    if which_clang:
        return str(Path(which_clang).parent)
    matches = glob.glob("/mnt/c/Users/*/AppData/Local/Microsoft/WinGet/Packages/MartinStorsjo.LLVM-MinGW*/**/bin/clang.exe", recursive=True)
    if matches:
        return str(Path(matches[0]).parent)
    return "/usr/bin"

bin_dir = find_bin_dir()
clang = os.path.join(bin_dir, "clang.exe")
clangpp = os.path.join(bin_dir, "clang++.exe")
ar = os.path.join(bin_dir, "llvm-ar.exe")

cpp_sources = [
    "FileStream.cpp",
    "SBaseCommon.cpp",
    "SBaseDumpData.cpp",
    "SBaseFileTable.cpp",
    "SBaseSubTypes.cpp",
    "SCompression.cpp",
    "SFileAddFile.cpp",
    "SFileAttributes.cpp",
    "SFileCompactArchive.cpp",
    "SFileCreateArchive.cpp",
    "SFileExtractFile.cpp",
    "SFileFindFile.cpp",
    "SFileGetFileInfo.cpp",
    "SFileListFile.cpp",
    "SFileOpenArchive.cpp",
    "SFileOpenFileEx.cpp",
    "SFilePatchArchives.cpp",
    "SFileReadFile.cpp",
    "SFileVerify.cpp",
    "SMemUtf8.cpp",
    "adpcm/adpcm.cpp",
    "huffman/huff.cpp",
    "sparse/sparse.cpp",
]

c_sources = [
    "jenkins/lookup3.c",
    "pklib/explode.c",
    "pklib/implode.c",
    "pklib/crc32.c",
    "LibTomCrypt.c",
    "LibTomMath.c",
    "LibTomMathDesc.c",
    # LZMA
    "lzma/C/LzFind.c",
    "lzma/C/LzmaDec.c",
    "lzma/C/LzmaEnc.c",
    # zlib
    "zlib/adler32.c",
    "zlib/compress.c",
    "zlib/crc32.c",
    "zlib/deflate.c",
    "zlib/inffast.c",
    "zlib/inflate.c",
    "zlib/inftrees.c",
    "zlib/trees.c",
    "zlib/zutil.c",
    # bzip2
    "bzip2/blocksort.c",
    "bzip2/bzlib.c",
    "bzip2/compress.c",
    "bzip2/crctable.c",
    "bzip2/decompress.c",
    "bzip2/huffman.c",
    "bzip2/randtable.c",
]

includes = [
    "-I.",
    "-Ilibtomcrypt/src/headers",
    "-Ilibtommath",
    "-Ilzma/C",
    "-Izlib",
    "-Ibzip2",
    "-DUNICODE",
    "-D_UNICODE",
    "-D_7ZIP_ST",
    "-Wno-deprecated-non-prototype",
    "-Wno-implicit-function-declaration",
]

import time

def run_cmd(cmd):
    for attempt in range(12):
        res = subprocess.run(cmd, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        if res.returncode == 0:
            return
        if "UtilAcceptVsock" in res.stderr or "vsock" in res.stderr or res.returncode == 1:
            time.sleep(0.5)
            continue
        print(f"Error running {cmd}:\n{res.stderr}")
        sys.exit(1)
    if res.returncode != 0:
        print(f"Failed running {cmd}:\n{res.stderr}")
        sys.exit(1)

objs = []
print("Compiling C++ files...")
for src in cpp_sources:
    obj = src.replace("/", "_").replace(".cpp", ".o")
    objs.append(obj)
    if not os.path.exists(obj) or os.path.getmtime(obj) < os.path.getmtime(src):
        cmd = [clangpp, "-O2", "-c", src, "-o", obj] + includes
        run_cmd(cmd)

print("Compiling C files...")
for src in c_sources:
    obj = src.replace("/", "_").replace(".c", ".o")
    objs.append(obj)
    if not os.path.exists(obj) or os.path.getmtime(obj) < os.path.getmtime(src):
        cmd = [clang, "-O2", "-c", src, "-o", obj] + includes
        run_cmd(cmd)

print(f"Archiving {len(objs)} objects into libstorm.a ...")
lib_path = "libstorm.a"
ar_cmd = [ar, "rcs", lib_path] + objs
run_cmd(ar_cmd)

print(f"StormLib built successfully: {lib_path} ({os.path.getsize(lib_path)} bytes)")

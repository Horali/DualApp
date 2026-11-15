#!/usr/bin/env bash
set -u
cd /c/Users/child/PROJECTS/DualApp/phase0b/tools/dualfs-tool
NDK=${NDK:-/c/Users/child/AppData/Local/Android/Sdk/ndk/29.0.13846066}
CC=$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android26-clang
$CC -O2 -Wall -Wextra -o dualfs-tool dualfs-tool.c
echo "EXIT=$?"
ls -l dualfs-tool

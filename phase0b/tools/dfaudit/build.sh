#!/usr/bin/env bash
set -u
cd /c/Users/child/PROJECTS/DualApp/phase0b/tools/dfaudit
NDK=${NDK:-/c/Users/child/AppData/Local/Android/Sdk/ndk/29.0.13846066}
CC=$NDK/toolchains/llvm/prebuilt/windows-x86_64/bin/aarch64-linux-android26-clang
$CC -O2 -Wall -Wextra -o dfaudit dfaudit.c
echo "EXIT=$?"
ls -l dfaudit

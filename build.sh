#!/bin/sh
# Cross-compile on Linux/WSL:  sudo apt install mingw-w64
set -e
cd "$(dirname "$0")"
x86_64-w64-mingw32-windres assets/livedock.rc -I assets -O coff -o /tmp/livedock_res.o
x86_64-w64-mingw32-g++ -Os -s -static -mwindows -fno-exceptions -fno-rtti -o LiveDock.exe src/livedock.cpp /tmp/livedock_res.o \
  -lgdiplus -lole32 -ldwmapi -lcomdlg32 -lshell32 -luser32 -lgdi32 -ladvapi32
echo built LiveDock.exe

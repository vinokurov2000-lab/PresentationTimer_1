#!/usr/bin/env bash
set -euo pipefail

zig rc /fo resources.res resources.rc
zig cc src/main.c src/timer.c src/renderer.c src/settings.c src/platform.c resources.res \
  -target x86_64-windows-gnu -municode -O2 -s -Wall -Wextra -Werror \
  -Wl,--subsystem,windows -luser32 -lgdi32 -lshell32 -lcomdlg32 -lwinmm -lm \
  -o PresentationTimer-v5.exe

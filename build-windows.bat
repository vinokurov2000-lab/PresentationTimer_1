@echo off
setlocal

where zig >nul 2>nul
if errorlevel 1 (
  echo Zig compiler was not found in PATH.
  exit /b 1
)

zig rc /fo resources.res resources.rc
if errorlevel 1 exit /b 1

zig cc src/main.c src/timer.c src/renderer.c src/settings.c src/platform.c resources.res ^
  -target x86_64-windows-gnu -municode -O2 -s -Wall -Wextra -Werror ^
  -Wl,--subsystem,windows -luser32 -lgdi32 -lshell32 -lcomdlg32 -lwinmm -lm ^
  -o PresentationTimer-v5.exe
if errorlevel 1 exit /b 1

echo PresentationTimer-v5.exe built successfully.
endlocal

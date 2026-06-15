# Project Agent Notes

## Encoding

This workspace lives in a path with Chinese characters and some project files are BOM-less UTF-8.
When reading text from Windows PowerShell, prefer:

- `Get-Content -Encoding UTF8 <path>`
- `Select-String -Encoding UTF8 ...`
- `rg` for search

## Build

Use the existing CMake preset flow:

```powershell
cmake --preset Debug --fresh
cmake --build --preset Debug
```

The expected firmware outputs are under `build/Debug/`.

## Current E1 Workflow

This repository is the E1 DShot300 same-RPM experiment firmware for the STM32F103C8T6 Blue Pill controller.

- The six P0 same-RPM profiles are compiled into `Core/Inc/app_e1_test.h`.
- Before `START`, the PB13 button cycles profiles in this order: `Si R1`, `Si R2`, `Si R3`, `GaN R1`, `GaN R2`, `GaN R3`.
- Each started session runs three cycles using the selected fixed profile.
- Each cycle steps directly to DShot `500`, holds it for `E1_RAMP_START_HOLD_MS = 250 ms`, then ramps to the selected profile within `E1_RAMP_MS = 5000` before the 60 s RUN window.
- The final main-branch firmware does not use the abandoned PB8 ESC signal reconnect experiment.
- `STOP` or `ABORT` over UART forces DShot stop and ends the current session.
- Safety faults stay latched; normal completion or manual stop re-arms to `WAIT_BT` after `E1_DONE_REARM_DELAY_MS = 5000`.
- The local OLED renderer in `Core/Src/app_e1_test.c` uses a small 5x7 glyph table. It currently covers digits, `.`, `-`, and uppercase `A-Z`; add glyphs before introducing other display characters.

## Git Hygiene

Do not stage unrelated local IDE changes. In this workspace, `.vscode/settings.json` may be locally modified and should not be included unless the user explicitly asks.

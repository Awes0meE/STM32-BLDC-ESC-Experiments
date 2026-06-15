# Project Agent Notes

## Encoding

This workspace lives in a path with Chinese characters and some project files may be BOM-less UTF-8.
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

The expected firmware outputs are under `build/Debug/`:

- `STM32_ESC_DSHOT_E2.elf`
- `STM32_ESC_DSHOT_E2.hex`
- `STM32_ESC_DSHOT_E2.bin`

## E2 Scope-Only Workflow

This repository is the E2 DShot300 same-RPM phase-node waveform firmware for the STM32F103C8T6 Blue Pill controller.

- The six P0 same-RPM profiles are compiled into `Core/Inc/app_e2_waveform.h`.
- Before `START`, PB13 cycles profiles in this order: `Si R1`, `Si R2`, `Si R3`, `GaN R1`, `GaN R2`, `GaN R3`.
- Serial profile selection is also supported before `START`: `P1` ... `P6`, or `SI_R1` ... `GAN_R3`.
- `START` runs one E2 capture session: `PREPARE -> RAMP -> STABILIZE -> RUN -> DONE`.
- `RAMP` steps to DShot `500`, holds for `E2_RAMP_START_HOLD_MS = 250 ms`, then ramps to the selected P0 command within `E2_RAMP_MS = 5000`.
- `STABILIZE` holds the selected DShot command for `E2_STABILIZE_MS = 10000` before scope capture.
- `RUN` is the scope capture window. It defaults to `E2_RUN_MS = 300000` and uses the selected fixed DShot command.
- PB0 is an optional oscilloscope marker: LOW outside capture, HIGH during `RUN`.
- `STOP` or `ABORT` over UART forces DShot stop, pulls PB0 LOW, and ends the current session.
- E2 uses only STM32-to-ESC throttle plus ground for the ESC control link. Do not reintroduce current-sense or voltage-sense ADC logic unless the experiment plan changes.
- Final E2 evidence is oscilloscope screenshots only. UART is control/status only; do not add FireWater, CSV, current/voltage/power telemetry, or final serial-log workflows.
- External E2 metadata should use the fixed P0 profile mapping for target RPM and DShot command. Do not require repeated laser tachometer readings for each E2 run.
- Normal completion or manual stop re-arms to `WAIT_BT` after `E2_DONE_REARM_DELAY_MS = 5000`.
- The local OLED renderer in `Core/Src/app_e2_waveform.c` uses a small 5x7 glyph table. It currently covers digits, `.`, `-`, and uppercase `A-Z`; add glyphs before introducing other display characters.

## Git Hygiene

This E2 workspace may not be a git repository. Do not copy `.git` or old `build/` artifacts from E1 unless explicitly requested.

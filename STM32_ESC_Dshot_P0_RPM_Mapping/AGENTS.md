# Project Notes For Future Agents

This repository is the P0 DShot-to-RPM mapping firmware, not the E1 steady-state firmware.

Active application entry points:

- `Core/Inc/app_p0_rpm_map.h`
- `Core/Src/app_p0_rpm_map.c`
- `Core/Src/main.c` calls `P0_RpmMap_Init()` and `P0_RpmMap_Task()`.

Build:

```bash
cmake --preset Debug --fresh
cmake --build --preset Debug
```

Expected firmware outputs are under `build/Debug/` with the base name `STM32_ESC_DSHOT_P0_RPM_MAPPING`.

User-facing documentation:

- `README.md` is the concise English overview.
- `docs/P0_RPM_Mapping_User_Guide_zh.md` is the detailed Chinese operating guide for building, flashing, running Si/GaN mapping sweeps, and converting logs to CSV.

Hardware and generated peripheral constraints:

- Preserve the E1 wiring: PB8/TIM4_CH3 DShot, PA0/PA1 ADC1 DMA, USART1 HC-05, PB12 HC-05 STATE, PB13 active-low button, PB6/PB7 I2C OLED, PC13 LED.
- Do not change DShot timing silently. `Core/Src/tim.c` currently uses `TIM4` with `PSC=0` and `ARR=239`; `Core/Src/app_p0_rpm_map.c` uses the carried-over DShot high ticks `90` and `180`.
- Avoid editing CubeMX-generated peripheral files unless the hardware/peripheral setup truly changes. Prefer adding app behavior in `app_p0_rpm_map.*` and root `CMakeLists.txt`.
- `Core/Inc/app_e1_test.h` and `Core/Src/app_e1_test.c` remain as reference code only; root `CMakeLists.txt` does not compile them.

P0 behavior to preserve:

- The firmware does not measure RPM directly. Default P0 operation is throttle-only: the user records handheld tachometer readings externally, and UART RPM entry is disabled by `P0_ENABLE_UART_RPM_INPUT=0`.
- The default load metadata is `prop`; normal propeller-mounted tests do not need a `LOAD prop` command.
- Default sweep advancement is manual. `P0_REQUIRE_NEXT_TO_ADVANCE=1` keeps each DShot command held until `NEXT`; in manual mode, `NEXT`/`REPEAT` are honored only in `MEASURE` after `# throttle_hold`, and `NEXT` jumps directly from the current command to the next command without a stop/rest gap.
- If a run starts from stop with a target command above `P0_HIGH_START_RAMP_THRESHOLD` (`800`), preserve the guarded high-start ramp from `600` to the target over `P0_HIGH_START_RAMP_MS`.
- Keep active-run timing conservative: `P0_BT_LOSS_ABORT_MS` debounces HC-05 STATE loss, and `P0_OLED_UPDATE_WHILE_ACTIVE=0` skips blocking OLED I2C refresh while DShot is active.
- Default sweep is `600..1600` by `100`; `1700/1800` require `EXTEND ON` or an explicit `SWEEP`/`CMD`.
- `STOP`, `ABORT`, active-state button press, and safety trips must force DShot `0` and keep sending stop frames.
- `# p0_step_summary` lines are the stable interface for log parsing with `tools/parse_p0_log.py`; in default throttle-only mode, RPM fields stay `0` and `status=throttle_only`.

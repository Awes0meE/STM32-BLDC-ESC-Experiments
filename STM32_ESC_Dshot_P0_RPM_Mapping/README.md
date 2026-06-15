# STM32_ESC_DSHOT_P0_RPM_MAPPING

STM32F103C8T6 Blue Pill firmware for the P0 DShot-to-RPM mapping pre-test.

P0 is a bench helper used before the same-RPM E1/E2/E3 ESC experiments. It holds selected raw DShot commands while the user measures RPM with a handheld laser tachometer and records readings externally. The serial log records the DShot command, voltage, current, and power for later manual DShot-to-RPM table assembly.

This firmware does not measure RPM directly. By default, RPM is measured manually with a handheld laser tachometer and written down outside the serial console.

Chinese usage guide: [docs/P0_RPM_Mapping_User_Guide_zh.md](docs/P0_RPM_Mapping_User_Guide_zh.md).

## Hardware

The project preserves the E1 controller wiring and CubeMX peripheral setup:

| Pin | Function |
| --- | --- |
| `PB8` | ESC signal output, `TIM4_CH3`, DShot |
| `PA0` | INA199 output, `ADC1_IN0` |
| `PA1` | VBAT divider midpoint, `ADC1_IN1` |
| `PA9` | HC-05 RXD, `USART1_TX` |
| `PA10` | HC-05 TXD, `USART1_RX` |
| `PB12` | HC-05 `STATE` input |
| `PB13` | Local button, active-low, internal pull-up |
| `PB6` / `PB7` | OLED `I2C1` SCL/SDA |
| `PC13` | Onboard status LED |
| `PA13` / `PA14` | SWDIO/SWCLK |

Keep `TIM4` at the generated DShot timing (`PSC=0`, `ARR=239`) and keep `DMA1_Channel5` enabled for `TIM4_CH3`.

## Workflow

1. Install either the Si ESC or GaN ESC on the same motor/load setup.
2. Connect the P0 controller hardware and open the HC-05 serial link.
3. Set the ESC label. The load label defaults to `prop`, matching the propeller-mounted test setup:

```text
BOARD Si
```

4. Optionally adjust the sweep:

```text
SWEEP 600 1600 100
EXTEND ON
CMD 900
```

5. Start:

```text
START
```

6. For each command point, wait for the target command to be applied and stabilized, measure RPM with the handheld tachometer, write it down, then send `NEXT` when you want the next throttle point:

```text
NEXT
```

The firmware emits a parseable `# p0_step_summary` line per command. In the default paper-recording mode, RPM fields remain zero and `status=throttle_only`.

## State Machine

Active application files:

- `Core/Inc/app_p0_rpm_map.h`
- `Core/Src/app_p0_rpm_map.c`

States:

- `WAIT_BT`: require stable HC-05 `STATE` when `P0_REQUIRE_BT=1`; sends DShot stop.
- `READY`: stopped, accepts setup commands and `START`.
- `PREPARE`: stopped safety countdown and zero-offset tracking.
- `RAMP`: ramps to the target DShot command when ramping is enabled for the step.
- `STABILIZE`: holds target command for speed settling.
- `MEASURE`: holds target command until `NEXT`, accumulates V/I/P averages.
- `REST`: sends DShot stop before the next point when legacy timed advance is enabled.
- `DONE`: all points finished, motor stopped.
- `ABORTED`: user stop, motor stopped.
- `FAULT`: safety trip, motor stopped, no auto-restart.

By default each point holds indefinitely and requires `NEXT` to advance. In manual mode, `NEXT` and `REPEAT` are accepted only after the point reaches `MEASURE` and the serial log has emitted `# throttle_hold ...`; duplicate or early commands during `RAMP` / `STABILIZE` are ignored. `NEXT` moves directly from the current DShot command to the next command without a stop/rest gap, so a sweep goes `600 -> 700 -> 800 ...` instead of repeatedly restarting from zero. Set `P0_REQUIRE_NEXT_TO_ADVANCE=0` to restore the older timed advance/rest behavior. Set `P0_JUMP_TO_TARGET_FROM_STOP=0` to restore the older linear ramp from stop. `P0_CONTINUOUS_SWEEP` is available as an explicit compile-time override.

If a run starts from stop with a target command above `800` such as `CMD 900` or `SWEEP 1000 1600 100`, the firmware applies a guarded high-start ramp: `600 -> target` over `P0_HIGH_START_RAMP_MS` (`6000 ms` by default). Normal sweeps starting at `600` still jump straight to `600`, and `NEXT` transitions while spinning remain direct.

`START` always enters `PREPARE` first and waits the configured countdown before the first DShot command is applied.

## Serial Commands

Commands are case-insensitive and accept `\r`, `\n`, or `\r\n`.
`LOAD` is optional because the firmware defaults to `prop`.

```text
HELP
STATUS
START
STOP
ABORT
BOARD Si
BOARD GaN
LOAD prop
LOAD noProp
NEXT
REPEAT
SWEEP <start> <end> <step>
EXTEND ON
EXTEND OFF
CMD <value>
```

Default sweep:

```text
600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600
```

`EXTEND ON` switches the default sweep to `600..1800` in `100`-command steps. The firmware does not automatically run `1700` or `1800` unless this command is used or an explicit `SWEEP`/`CMD` requests it.

## Button

`PB13` is active-low.

- `READY` / `DONE` / `ABORTED`: short press cycles the preview command shown on the OLED; long press starts mapping.
- `RAMP` / `STABILIZE` / `MEASURE`: any press aborts and sends DShot stop.

UART RPM input is disabled by default (`P0_ENABLE_UART_RPM_INPUT=0`) because readings are expected to be recorded externally.

## OLED

The OLED shows five lines while the motor is stopped or preparing:

```text
VBAT 14.720V
CURR 1.340A
POWR 19.728W
P0 CMD0800
PREP 03S
```

By default `P0_OLED_UPDATE_WHILE_ACTIVE=0`, so OLED refresh is skipped while DShot is active. This avoids long blocking I2C transfers disturbing the DShot frame cadence at higher throttle. During active testing, use the serial `# p0_step_start`, `# throttle_hold`, `# fault`, and `# abort` lines as the authoritative state log. Set `P0_OLED_UPDATE_WHILE_ACTIVE=1` only if live OLED refresh is more important than DShot timing margin.

## Serial Output

Helper lines begin with `#`, for example:

```text
# boot app=P0_RPM_MAPPING version=0.1 uart=9600 protocol=firewater
# ready board=UNKNOWN load=prop sweep_count=11
# p0_start board=Si load=prop count=11
# p0_step_start board=Si load=prop index=0 cmd=600
# throttle_hold board=Si load=prop index=0 cmd=600 action=measure_on_paper_send_NEXT
# p0_step_summary date=NA board=Si load_mode=prop index=0 step_index=0 cmd=600 dshot_cmd=600 rpm1=0 rpm2=0 rpm3=0 rpm_used=0 rpm_mean=0.0 rpm_min=0 rpm_max=0 rpm_range=0 vbat_mean_V=14.720 current_mean_A=1.340 power_mean_W=19.724 status=throttle_only
# p0_done board=Si load=prop points=11
```

High-start protected runs also emit `# p0_ramp_start ... from=600 to=<target> ms=6000` before `# p0_step_start`.

Continuous FireWater/VOFA-friendly frames are emitted every `P0_CSV_INTERVAL_MS`:

```text
p0:t_ms,state,step_index,cmd_raw,dshot_cmd,adc_i_raw,adc_vbat_raw,v_i_sense,v_vbat_adc,current_A,vbat_V,power_W,zero_offset_V,active_zero_offset_V,delta_i_V,rpm_count,rpm1,rpm2,rpm3,rpm_used,flags
```

## Analog And Safety

ADC uses two-channel scan with DMA circular mode:

- `PA0 / ADC1_IN0`: current sense voltage
- `PA1 / ADC1_IN1`: VBAT divider voltage

Current calculation is configured in `Core/Inc/app_p0_rpm_map.h`:

- `VBAT_DIVIDER_RATIO`
- `INA199_GAIN_V_V`
- `CURRENT_SHUNT_RESISTANCE_OHM`
- `CURRENT_SCALE_A_PER_V`
- `P0_CURRENT_SIGN_INVERT`
- `P0_ZERO_TRACK_DELAY_MS`
- `P0_ZERO_TRACK_ALPHA_STOP`

Safety defaults are conservative placeholders and must be tuned for the bench setup:

- `P0_CURRENT_TRIP_A = 8.0f`
- `P0_CURRENT_TRIP_HOLD_MS = 300`
- `P0_ENABLE_CURRENT_TRIP = 1`
- `P0_VBAT_MIN_V = 9.0f`
- `P0_ENABLE_VBAT_TRIP = 0`

`STOP`, `ABORT`, active-state button press, and safety trips all force DShot `0` and keep sending stop.

## Log Parsing

Parse saved serial logs into mapping CSV:

```bash
python -B tools/parse_p0_log.py --input 2026-05-12_P0_Si_mapping_prop.txt --output RPM_mapping_Si.csv
python -B tools/parse_p0_log.py --input 2026-05-12_P0_GaN_mapping_prop.txt --output RPM_mapping_GaN.csv
```

CSV columns:

```text
date,board,load_mode,step_index,dshot_cmd,rpm1,rpm2,rpm3,rpm_used,rpm_mean,rpm_min,rpm_max,rpm_range,vbat_mean_V,current_mean_A,power_mean_W,status
```

## Build

```bash
cmake --preset Debug --fresh
cmake --build --preset Debug
```

Expected outputs:

```text
build/Debug/STM32_ESC_DSHOT_P0_RPM_MAPPING.elf
build/Debug/STM32_ESC_DSHOT_P0_RPM_MAPPING.hex
build/Debug/STM32_ESC_DSHOT_P0_RPM_MAPPING.bin
```

## Limitations

- P0 is a calibration/logging helper, not RPM closed-loop control.
- The STM32 does not measure RPM unless extra tachometer hardware is added in a future design.
- `# p0_step_summary` lines support later CSV extraction; they do not prove ESC efficiency by themselves.
- The old E1 files remain in the tree as reference code, but they are not compiled by the P0 CMake target.

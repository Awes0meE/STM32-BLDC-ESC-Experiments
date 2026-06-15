<div align="center">

# STM32_ESC_Dshot_E1

**STM32F103C8T6-based E1 test firmware for ESC steady-state comparison experiments**

**基于 `STM32F103C8T6` 的 E1 实验固件，用于 ESC 稳态对比测试**

<p>
  <img src="https://img.shields.io/badge/MCU-STM32F103C8T6-03234B" alt="MCU">
  <img src="https://img.shields.io/badge/Firmware-E1--Final--DShot300-2ea44f" alt="Firmware">
  <img src="https://img.shields.io/badge/Protocol-DShot300-orange" alt="Protocol">
  <img src="https://img.shields.io/badge/Platform-Blue%20Pill-1f6feb" alt="Platform">
  <img src="https://img.shields.io/badge/Use%20Case-ESC%20Steady--State%20Comparison-purple" alt="Use Case">
</p>

</div>

---

## Table of Contents

- [English](#english)
  - [E1 Test Firmware](#e1-test-firmware)
  - [1. Project Purpose](#1-project-purpose)
  - [2. Current E1 Workflow](#2-current-e1-workflow)
  - [3. E1 State Machine](#3-e1-state-machine)
  - [4. DShot Command Strategy](#4-dshot-command-strategy)
  - [5. Bluetooth / Serial Control](#5-bluetooth--serial-control)
  - [6. Button Behavior](#6-button-behavior)
  - [7. OLED Display](#7-oled-display)
  - [8. FireWater / Serial Output](#8-firewater--serial-output)
  - [9. Analog Sampling and Current Chain](#9-analog-sampling-and-current-chain)
  - [10. Hardware Connections](#10-hardware-connections)
  - [11. CubeMX Notes](#11-cubemx-notes)
  - [12. Key Configurable Macros](#12-key-configurable-macros)
  - [13. Build](#13-build)
  - [14. Current Scope](#14-current-scope)
  - [15. E1-V2.0-DShot300 Update](#15-e1-v20-dshot300-update)
  - [16. E1-V3.0-DShot300 Update](#16-e1-v30-dshot300-update)
  - [17. E1-V4.0-DShot300 Update](#17-e1-v40-dshot300-update)
  - [18. E1-Final-DShot300 Final Release](#18-e1-final-dshot300-final-release)
- [中文](#中文)
  - [E1 实验固件说明](#e1-实验固件说明)
  - [1. 项目用途](#1-项目用途)
  - [2. 当前 E1 实验流程](#2-当前-e1-实验流程)
  - [3. E1 状态机](#3-e1-状态机)
  - [4. DShot 命令策略](#4-dshot-命令策略)
  - [5. 蓝牙 / 串口控制](#5-蓝牙--串口控制)
  - [6. 按钮行为](#6-按钮行为)
  - [7. OLED 显示内容](#7-oled-显示内容)
  - [8. FireWater / 串口输出](#8-firewater--串口输出)
  - [9. 模拟采样与电流链](#9-模拟采样与电流链)
  - [10. 硬件连接](#10-硬件连接)
  - [11. CubeMX 配置要点](#11-cubemx-配置要点)
  - [12. 关键可调宏](#12-关键可调宏)
  - [13. 编译](#13-编译)
  - [14. 当前版本边界](#14-当前版本边界)
  - [15. E1-V2.0-DShot300 版本更新](#15-e1-v20-dshot300-版本更新)
  - [16. E1-V3.0-DShot300 版本更新](#16-e1-v30-dshot300-版本更新)
  - [17. E1-V4.0-DShot300 版本更新](#17-e1-v40-dshot300-版本更新)
  - [18. E1-Final-DShot300 最终版](#18-e1-final-dshot300-最终版)

---

# English

## E1 Test Firmware

This repository contains the current E1 firmware used for ESC steady-state comparison work.

The firmware runs on a Blue Pill (`STM32F103C8T6`) board and acts as a lightweight experiment controller and data acquisition node.

It is intended for **steady-state same-RPM operating point comparison**, especially for comparing different ESC variants under the same power supply, motor, propeller, and target-RPM points while using board-specific P0 DShot commands.

> [!NOTE]
> This README is organized in English first and Chinese second.  
> The technical content is kept consistent with the current repository description.

---

## 1. Project Purpose

This repository contains the current E1 firmware used on a Blue Pill (`STM32F103C8T6`) board.

The board acts as a lightweight experiment controller and data acquisition node for ESC comparison work.

### Main responsibilities

- output `DShot300` throttle to the ESC
- collect current and battery voltage using `ADC + DMA`
- communicate with the PC through an `HC-05` Bluetooth serial link
- display live status on a `1.3" I2C OLED`
- provide a simple local button to select the compiled P0 same-RPM profile before test start

This E1 firmware is intended for **steady-state same-RPM operating point comparison**, especially for comparing ESC variants under the same power supply, motor, propeller, and target-RPM points while using board-specific P0 DShot commands.

---

## 2. Current E1 Workflow

The current logic is:

1. power on
2. enter `WAIT_BT`
3. keep sending DShot `stop`
4. wait for Bluetooth link confirmation from `HC-05 STATE`
5. after Bluetooth is confirmed, wait for serial command `START`
6. enter `PREPARE` for 10 s
7. step cycle 1 directly to DShot `500`, hold it briefly, then ramp to the selected P0 profile command within the 5 s `RAMP` window
8. run cycle 1 at the selected profile command for 60 s
9. rest at DShot stop for 30 s
10. step cycle 2 directly to DShot `500`, hold it briefly, then ramp to the same selected profile command within the 5 s `RAMP` window
11. run cycle 2 at the same selected profile command for 60 s
12. rest at DShot stop for 30 s
13. step cycle 3 directly to DShot `500`, hold it briefly, then ramp to the same selected profile command within the 5 s `RAMP` window
14. run cycle 3 at the same selected profile command for 60 s
15. enter `DONE`, then re-arm to `WAIT_BT` after 5 s when the run completed normally or was stopped by `STOP`

The MCU performs the timing locally. The PC side only needs to connect, choose the P0 same-RPM profile with the button if needed, and send `START`.

### Workflow Diagram

```mermaid
flowchart TD
    A[Power On] --> B[WAIT_BT]
    B --> C[Keep sending DShot stop]
    C --> D[Wait for HC-05 STATE confirmation]
    D --> E[Wait for serial command START]
    E --> F[PREPARE 10 s]
    F --> G[RAMP cycle 1, 5 s]
    G --> H[RUN cycle 1, 60 s]
    H --> I[REST 30 s]
    I --> J[RAMP cycle 2, 5 s]
    J --> K[RUN cycle 2, 60 s]
    K --> L[REST 30 s]
    L --> M[RAMP cycle 3, 5 s]
    M --> N[RUN cycle 3, 60 s]
    N --> O[DONE]
    O --> P[WAIT_BT re-armed after 5 s]
```

---

## 3. E1 State Machine

State enum is defined in [`Core/Inc/app_e1_test.h`](./Core/Inc/app_e1_test.h):

- `STATE_WAIT_BT`
- `STATE_PREPARE`
- `STATE_RAMP`
- `STATE_RUN`
- `STATE_STOP`
- `STATE_DONE`
- `STATE_IDLE`

### Main entry points

- `E1_Test_Init()`
- `E1_Test_Task()`

### Core control implementation

- [`Core/Src/app_e1_test.c`](./Core/Src/app_e1_test.c)

---

## 4. DShot Command Strategy

The transport is `DShot300`.

### Current behavior

- `0` means stop
- valid throttle range is `48 ~ 2047`
- the current default profile is `Si R1`
- the P0 same-RPM profiles are compiled into the firmware:

| Profile | Board | RPM point | Target RPM | DShot command |
|---:|---|---|---:|---:|
| 1 | Si | R1 | 3000 | 551 |
| 2 | Si | R2 | 6500 | 786 |
| 3 | Si | R3 | 10000 | 1124 |
| 4 | GaN | R1 | 3000 | 540 |
| 5 | GaN | R2 | 6500 | 762 |
| 6 | GaN | R3 | 10000 | 1022 |

Before the test starts, the local button cycles through these six fixed P0 profiles.

---

## 5. Bluetooth / Serial Control

The current E1 firmware uses `HC-05` Bluetooth serial for experiment triggering and logging.

### Behavior

- `HC-05 STATE` is monitored as a Bluetooth connection indicator
- after connection is stable for `500 ms`, the firmware prints boot / ready messages
- the test starts only after receiving the serial command:

```text
START
```

- during an active run, the serial command `STOP` or `ABORT` forces DShot stop and ends the current session
- after normal completion or manual stop, the controller re-arms in `WAIT_BT` after 5 s; safety faults stay latched and should be handled with a reset / power check

### Supported characteristics

- case-insensitive parsing
- tolerant to optional newline behavior
- sends an ACK before entering prepare state

---

## 6. Button Behavior

The current button is used only before the test starts.

### Short press behavior

- valid in `WAIT_BT`
- only valid after Bluetooth is confirmed
- only valid before `START`
- each press selects the next P0 same-RPM profile
- after profile 6, it wraps back to profile 1

---

## 7. OLED Display

The OLED shows 5 lines:

1. `VBAT`
2. `CURR`
3. `POWR`
4. selected P0 profile, board/RPM point, DShot command, and cycle; during `RAMP`, this shows current command and target command
5. state-dependent timing information

The built-in 5x7 OLED font covers digits, `.`, `-`, and uppercase `A-Z`, including the `GaN`, `RUN`, and `DONE` labels used by the current workflow.

### Typical line 5 content

- `PREP 09S`
- `RUN1 12S`
- `REST 28S`

### Additional visual prompt in `RUN`

- blink `R` around `25 ~ 30 s` in each run cycle as reminder for RPM measurement at 30 s
- blink `H` around `35 ~ 40 s` in each run cycle as reminder for hotspot / thermal measurement at 40 s

---

## 8. FireWater / Serial Output

The firmware outputs FireWater-compatible lines over UART so that tools such as VOFA can parse them.

### Current frame format

```text
h1:t_ms,state,cmd_raw,dshot_cmd,adc_i_raw,adc_vbat_raw,v_i_sense,v_vbat_adc,current_A,vbat_V,power_W,zero_offset_V,active_zero_offset_V,delta_i_V,profile,target_rpm,instant_current_A
```

Lines are terminated with `\r\n`.

The firmware also prints human-readable helper lines such as:

- `# boot`
- `# connected`
- `# waiting`
- `# ack`
- `# state_change`
- `# profile_set`
- `# start_profile`
- `# user_stop`
- `# safety_fault`
- `# rearmed`

---

## 9. Analog Sampling and Current Chain

### ADC acquisition

- `ADC1 + DMA`
- 2 channels
- circular mode
- averaged in firmware

### Channels

- `ADC1_IN0 / PA0`: INA199 output
- `ADC1_IN1 / PA1`: VBAT divider midpoint

### Derived values

- `v_i_sense`
- `v_vbat_adc`
- `current_A`
- `instant_current_A`
- `vbat_V`
- `power_W`
- `delta_i_V`
- `active_zero_offset_V`

### Current calculation path

```text
active_zero_offset_V = baseline_zero_offset_voltage
delta_i_V = v_i_sense - active_zero_offset_V
signed_delta_i_V = delta_i_V or -delta_i_V depending on E1_CURRENT_SIGN_INVERT
CURRENT_SCALE_A_PER_V = 1 / (INA199_GAIN_V_V * CURRENT_SHUNT_RESISTANCE_OHM)
instant_current_A = max(signed_delta_i_V * CURRENT_SCALE_A_PER_V, 0)
current_A = moving_average(instant_current_A, E1_CURRENT_FILTER_WINDOW_MS)
power_W   = current_A * vbat_V
```

Default hardware constants in this repo are now:

- `INA199_GAIN_V_V = 200` (INA199A3)
- `CURRENT_SHUNT_RESISTANCE_OHM = 0.00025`
- `CURRENT_SCALE_A_PER_V = 20`

Current polarity is currently set by:

- `E1_CURRENT_SIGN_INVERT = 0`

The firmware uses:

- `baseline_zero_offset_voltage`
- `active_zero_offset_voltage`

### Zero baseline behavior

- zero samples are collected only while the current DShot output is `0`
- each started session resets the zero baseline and normally completes it during `PREPARE`
- `E1_BASELINE_SAMPLE_COUNT = 600` samples are taken at `E1_ZERO_OFFSET_SAMPLE_INTERVAL_MS = 10 ms`
- during `RAMP` and `RUN`, the zero baseline is frozen because the DShot output is non-zero
- `STOP` and `DONE` only continue zero sampling if the 600-sample baseline has not already completed

---

## 10. Hardware Connections

- `PB8` -> ESC signal (`TIM4_CH3`, DShot300)
- `PA0` <- INA199 output (`ADC1_IN0`)
- `PA1` <- VBAT divider midpoint (`ADC1_IN1`)
- `PA9` -> HC-05 RXD (`USART1_TX`)
- `PA10` <- HC-05 TXD (`USART1_RX`)
- `PB12` <- HC-05 STATE
- `PB13` <- P0 profile select button, active-low, internal pull-up
- `PB6 / PB7` -> OLED (`I2C1`)
- `PC13` -> onboard status LED
- `PA13 / PA14` -> SWD

---

## 11. CubeMX Notes

Important assumptions for the current E1 project:

- `TIM4_CH3` on `PB8` for DShot output
- `USART1` enabled for HC-05 serial
- `PB12` configured as Bluetooth state input
- `PB13` configured as button input with pull-up
- `I2C1` on `PB6 / PB7`
- `ADC1` with 2-channel scan + DMA circular

### Critical timing requirement

- `TIM4` must stay in DShot configuration (`PSC=0`, `ARR=239`)

### Also ensure

- `DMA1_Channel5` interrupt is enabled for TIM4 CH3 DMA

---

## 12. Key Configurable Macros

Defined in [`Core/Inc/app_e1_test.h`](./Core/Inc/app_e1_test.h):

### Timing

- `E1_BT_PREPARE_MS`
- `E1_BT_CONNECT_CONFIRM_MS`
- `E1_RAMP_MS`
- `E1_RAMP_START_HOLD_MS`
- `E1_RUN_MS`
- `E1_TEST_CYCLE_COUNT`
- `E1_REST_MS`
- `E1_DONE_REARM_DELAY_MS`
- `E1_SESSION_MAX_MS`
- `E1_CSV_INTERVAL_MS`

### UART / Bluetooth Robustness

- `E1_UART_BOOT_QUIET_MS`
- `E1_UART_TX_BUFFER_SIZE`

### DShot

- `E1_RUN_THROTTLE_DSHOT`
- `E1_RUN_THROTTLE_MIN_DSHOT`
- `E1_RUN_THROTTLE_MAX_DSHOT`
- `E1_P0_SI_R1_DSHOT` / `E1_P0_SI_R2_DSHOT` / `E1_P0_SI_R3_DSHOT`
- `E1_P0_GAN_R1_DSHOT` / `E1_P0_GAN_R2_DSHOT` / `E1_P0_GAN_R3_DSHOT`
- `E1_RAMP_START_DSHOT`
- `E1_DSHOT_SEND_INTERVAL_MS`

### Analog

- `VBAT_DIVIDER_RATIO`
- `INA199_GAIN_V_V`
- `CURRENT_SHUNT_RESISTANCE_OHM`
- `CURRENT_SCALE_A_PER_V`
- `E1_CURRENT_SIGN_INVERT`
- `E1_ZERO_OFFSET_SAMPLE_INTERVAL_MS`
- `E1_BASELINE_SAMPLE_COUNT`
- `E1_CURRENT_FILTER_SAMPLE_INTERVAL_MS`
- `E1_CURRENT_FILTER_WINDOW_MS`

### Safety

- `E1_CURRENT_TRIP_A`
- `E1_CURRENT_TRIP_HOLD_MS`
- `E1_CURRENT_FAST_TRIP_A`
- `E1_CURRENT_FAST_TRIP_HOLD_MS`

### Display

- `E1_OLED_UPDATE_INTERVAL_MS`
- `E1_OLED_I2C_TIMEOUT_MS`
- `E1_OLED_POWERUP_DELAY_MS`
- `E1_OLED_RETRY_INTERVAL_MS`
- `E1_OLED_I2C_ADDR`
- `E1_OLED_COLUMN_OFFSET`

---

## 13. Build

### Example

```bash
cmake --preset Debug --fresh
cmake --build --preset Debug
```

### Expected outputs

- `build/Debug/STM32_ESC_DSHOT_E1.elf`
- `build/Debug/STM32_ESC_DSHOT_E1.hex`
- `build/Debug/STM32_ESC_DSHOT_E1.bin`

---

## 14. Current Scope

This repository currently represents the stabilized E1 steady-state experiment version.

It is intended for:

- compiled P0 same-RPM profile tests
- fixed DShot command during each selected profile's `RUN` window
- current / voltage / power logging
- OLED-assisted bench operation
- Bluetooth-triggered experiment start

It does **not** implement:

- RPM closed-loop control
- H2 step-response workflow and after experiments

---

## 15. E1-V2.0-DShot300 Update

This update finalizes a more flight-representative transport setup by changing the throttle protocol timing from `DShot500` to `DShot300`.

In addition, three bench-use improvements are included:

- OLED startup robustness is improved with delayed initialization and background retry, so the display no longer depends on a manual reset after power-up
- `power_W` is now calculated directly as `vbat_V * current_A`
- negative current results are clamped to `0`, preventing negative current and negative power values from appearing in the steady-state logs

This version is intended to be the `E1-V2.0-DShot300` milestone for the current steady-state ESC comparison workflow.

---

## 16. E1-V3.0-DShot300 Update

This release keeps `DShot300` and focuses on safer high-throttle bench testing.

Main changes:

- six compiled P0 same-RPM profiles are now selected locally before `START`
- automatic 3-cycle test flow: each cycle ramps, runs for 60 s, and rests before the next cycle
- soft throttle ramp: each cycle starts from `E1_RAMP_START_DSHOT = 0` and reaches the selected P0 profile command over `E1_RAMP_MS = 3000`
- serial output rate reduced to `E1_CSV_INTERVAL_MS = 1000` to reduce VOFA / Bluetooth buffer pressure
- UART output is non-blocking and old queued data is cleared when a new test starts
- Bluetooth reset / reconnect behavior is hardened to avoid unsolicited boot spam into VOFA
- over-current safety latch and forced DShot stop are enabled during both `RAMP` and `RUN`
- fast over-current detection uses the instantaneous ADC average before the display/log smoothing window
- OLED I2C timeout is tuned for the 100 kHz bus while still avoiding long display-related stalls

The active P0 profile table uses Si commands `551 / 786 / 1124` and GaN commands `540 / 762 / 1022` for the `3000 / 6500 / 10000 RPM` targets.

---

## 17. E1-V4.0-DShot300 Update

This release keeps `DShot300` and aligns the firmware, OLED UI, and README with the current P0 same-RPM E1 workflow.

Main changes:

- embeds six P0 same-RPM profiles for Si and GaN boards
- uses the PB13 button to cycle profiles after Bluetooth confirmation and before `START`
- runs three local cycles using the selected profile command, with a 3 s ramp from `0` before each 60 s `RUN`
- keeps `STOP` / `ABORT`, safety fault latching, and post-run re-arm behavior documented with the current code
- extends the OLED 5x7 font to cover uppercase `A-Z`, so labels such as `GaN`, `RUN`, and `DONE` render correctly
- updates the README wording to same-RPM profile comparison

---

## 18. E1-Final-DShot300 Final Release

This release marks the E1 experiment campaign as complete and captures the final same-RPM data-collection firmware.

Main notes:

- the active firmware remains the DShot300 P0 same-RPM profile workflow with three automatic cycles
- each cycle steps directly to DShot `500`, holds for `E1_RAMP_START_HOLD_MS = 250 ms`, and ramps to the selected profile command within `E1_RAMP_MS = 5000`
- the abandoned PB8 ESC signal reconnect branch is intentionally not included in `main` or this release
- README and project handoff notes are synchronized for final archival use

---

# 中文

## E1 实验固件说明

本仓库保存了当前用于 ESC 稳态对比实验的 E1 固件版本。

该固件运行在 Blue Pill（`STM32F103C8T6`）开发板上，作为一个轻量级实验控制器和数据采集节点使用。

它主要用于**稳态同 RPM 工况对比实验**，尤其适用于在相同电源、电机、螺旋桨和目标 RPM 点下，使用不同板子各自的 P0 DShot 命令对不同 ESC 方案进行横向比较。

> [!NOTE]
> 本 README 采用先英文后中文的结构。  
> 具体技术内容保持与当前仓库描述一致。

---

## 1. 项目用途

这个仓库保存了当前 E1 实验所用的 Blue Pill（`STM32F103C8T6`）固件。

这块板子作为一个轻量级实验控制器和数据采集节点，服务于 ESC 对比实验。

### 主要职责

- 向 ESC 输出 `DShot300` 油门命令
- 通过 `ADC + DMA` 采集电流和电池电压
- 通过 `HC-05` 蓝牙串口和电脑通信
- 在 `1.3 寸 I2C OLED` 上显示实时状态
- 在测试开始前通过本地按钮选择已写入固件的 P0 同 RPM profile

这份 E1 固件的目标是做**稳态同 RPM 工况对比**，尤其适用于在相同电源、电机、桨和目标 RPM 点下，使用不同板子各自的 P0 DShot 命令比较不同 ESC 的表现。

---

## 2. 当前 E1 实验流程

当前逻辑流程如下：

1. 上电
2. 进入 `WAIT_BT`
3. 持续发送 DShot `stop`
4. 等待 `HC-05 STATE` 确认蓝牙已连接
5. 蓝牙确认后等待串口命令 `START`
6. 进入 `PREPARE`，持续 10 秒
7. 第 1 轮先直接打到 DShot `500`，短暂保持后，在 5 秒 `RAMP` 窗口内缓坡到选定 P0 profile 命令
8. 第 1 轮按选定 profile 命令运行 60 秒
9. DShot stop 休息 30 秒
10. 第 2 轮先直接打到 DShot `500`，短暂保持后，在 5 秒 `RAMP` 窗口内缓坡到同一选定 profile 命令
11. 第 2 轮按同一选定 profile 命令运行 60 秒
12. DShot stop 休息 30 秒
13. 第 3 轮先直接打到 DShot `500`，短暂保持后，在 5 秒 `RAMP` 窗口内缓坡到同一选定 profile 命令
14. 第 3 轮按同一选定 profile 命令运行 60 秒
15. 进入 `DONE`，正常完成或手动 `STOP` 时 5 秒后重新回到 `WAIT_BT`

整个时序由 STM32 本地执行，PC 端只需要连上蓝牙、按需用按钮选择 P0 同 RPM profile，并发送 `START`。

### 流程图

```mermaid
flowchart TD
    A[上电] --> B[WAIT_BT]
    B --> C[持续发送 DShot stop]
    C --> D[等待 HC-05 STATE 确认蓝牙连接]
    D --> E[等待串口命令 START]
    E --> F[PREPARE 10 s]
    F --> G[RAMP cycle 1, 5 s]
    G --> H[RUN cycle 1, 60 s]
    H --> I[REST 30 s]
    I --> J[RAMP cycle 2, 5 s]
    J --> K[RUN cycle 2, 60 s]
    K --> L[REST 30 s]
    L --> M[RAMP cycle 3, 5 s]
    M --> N[RUN cycle 3, 60 s]
    N --> O[DONE]
    O --> P[5 s 后重新回到 WAIT_BT]
```

---

## 3. E1 状态机

状态枚举定义在 [`Core/Inc/app_e1_test.h`](./Core/Inc/app_e1_test.h)：

- `STATE_WAIT_BT`
- `STATE_PREPARE`
- `STATE_RAMP`
- `STATE_RUN`
- `STATE_STOP`
- `STATE_DONE`
- `STATE_IDLE`

### 主入口函数

- `E1_Test_Init()`
- `E1_Test_Task()`

### 核心控制实现位于

- [`Core/Src/app_e1_test.c`](./Core/Src/app_e1_test.c)

---

## 4. DShot 命令策略

当前底层协议是 `DShot300`。

### 当前行为

- `0` 表示停机
- 有效油门范围为 `48 ~ 2047`
- 当前默认 profile 为 `Si R1`
- P0 同 RPM profile 已直接写入固件：

| Profile | 板子 | RPM 点 | 目标 RPM | DShot 命令 |
|---:|---|---|---:|---:|
| 1 | Si | R1 | 3000 | 551 |
| 2 | Si | R2 | 6500 | 786 |
| 3 | Si | R3 | 10000 | 1124 |
| 4 | GaN | R1 | 3000 | 540 |
| 5 | GaN | R2 | 6500 | 762 |
| 6 | GaN | R3 | 10000 | 1022 |

在测试正式开始之前，可以通过按钮在这 6 个固定 P0 profile 之间循环选择。

---

## 5. 蓝牙 / 串口控制

当前 E1 版本使用 `HC-05` 蓝牙串口来完成实验触发和日志输出。

### 行为如下

- 监测 `HC-05 STATE` 作为蓝牙连接状态信号
- 当连接稳定保持 `500 ms` 后，固件会打印 boot / ready 提示
- 只有收到串口命令：

```text
START
```

才会真正开始实验

测试运行中也可以发送 `STOP` 或 `ABORT`，固件会立即强制 DShot stop 并结束当前 session。

正常完成或手动停止后，控制器会在 5 秒后重新回到 `WAIT_BT`；过流等 safety fault 会保持锁存，应复位并检查硬件后再继续。

### 当前命令识别特性

- 大小写不敏感
- 对换行要求比较宽松
- 在进入准备阶段前会先返回 ACK

---

## 6. 按钮行为

当前按钮只用于测试开始前调整油门。

### 短按行为

- 只在 `WAIT_BT` 状态有效
- 只有蓝牙连接确认后才有效
- 只有在收到 `START` 之前有效
- 每按一次，切换到下一个 P0 同 RPM profile
- 到 profile 6 后再次按下会回到 profile 1

---

## 7. OLED 显示内容

OLED 当前显示 5 行：

1. `VBAT`
2. `CURR`
3. `POWR`
4. 当前 P0 profile、板子/RPM 点、DShot 命令和重复轮次；在 `RAMP` 阶段显示当前命令和目标命令
5. 状态相关的时间信息

内置 5x7 OLED 字库覆盖数字、`.`、`-` 和大写 `A-Z`，包括当前流程会用到的 `GaN`、`RUN`、`DONE` 等标签。

### 第 5 行典型显示内容例如

- `PREP 09S`
- `RUN1 12S`
- `REST 28S`

### 在 `RUN` 状态下还有额外视觉提示

- 每轮 `RUN` 的 `25 ~ 30 s` 附近闪烁 `R`，提示 30 秒测速
- 每轮 `RUN` 的 `35 ~ 40 s` 附近闪烁 `H`，提示 40 秒测热点

---

## 8. FireWater / 串口输出

固件通过 UART 输出兼容 FireWater 的数据帧，便于 VOFA 等工具解析。

### 当前数据帧格式

```text
h1:t_ms,state,cmd_raw,dshot_cmd,adc_i_raw,adc_vbat_raw,v_i_sense,v_vbat_adc,current_A,vbat_V,power_W,zero_offset_V,active_zero_offset_V,delta_i_V,profile,target_rpm,instant_current_A
```

每行都以 `\r\n` 结束。

同时固件也会输出一些便于人工查看的辅助提示行，例如：

- `# boot`
- `# connected`
- `# waiting`
- `# ack`
- `# state_change`
- `# profile_set`
- `# start_profile`
- `# user_stop`
- `# safety_fault`
- `# rearmed`

---

## 9. 模拟采样与电流链

### ADC 采样方式

- `ADC1 + DMA`
- 双通道
- 循环模式
- 固件内做平均

### 采样通道

- `ADC1_IN0 / PA0`：INA199 输出
- `ADC1_IN1 / PA1`：VBAT 分压中点

### 导出量包括

- `v_i_sense`
- `v_vbat_adc`
- `current_A`
- `instant_current_A`
- `vbat_V`
- `power_W`
- `delta_i_V`
- `active_zero_offset_V`

### 当前电流计算路径

```text
active_zero_offset_V = baseline_zero_offset_voltage
delta_i_V = v_i_sense - active_zero_offset_V
signed_delta_i_V = delta_i_V or -delta_i_V depending on E1_CURRENT_SIGN_INVERT
CURRENT_SCALE_A_PER_V = 1 / (INA199_GAIN_V_V * CURRENT_SHUNT_RESISTANCE_OHM)
instant_current_A = max(signed_delta_i_V * CURRENT_SCALE_A_PER_V, 0)
current_A = moving_average(instant_current_A, E1_CURRENT_FILTER_WINDOW_MS)
power_W   = current_A * vbat_V
```

当前极性配置为：

- `E1_CURRENT_SIGN_INVERT = 0`

固件内部使用两套零点：

- `baseline_zero_offset_voltage`
- `active_zero_offset_voltage`

### 零点基线策略

- 只有当前 DShot 输出为 `0` 时才采集零点样本
- 每次收到 `START` 后会重置零点基线，并通常在 `PREPARE` 阶段完成
- `E1_BASELINE_SAMPLE_COUNT = 600`，采样间隔为 `E1_ZERO_OFFSET_SAMPLE_INTERVAL_MS = 10 ms`
- `RAMP` 和 `RUN` 阶段因为 DShot 输出非零，会冻结零点基线
- `STOP` 和 `DONE` 只会在 600 个零点样本尚未采满时继续采样

---

## 10. 硬件连接

- `PB8` -> ESC 信号输出（`TIM4_CH3`, DShot300）
- `PA0` <- INA199 输出（`ADC1_IN0`）
- `PA1` <- 电池分压中点（`ADC1_IN1`）
- `PA9` -> HC-05 RXD（`USART1_TX`）
- `PA10` <- HC-05 TXD（`USART1_RX`）
- `PB12` <- HC-05 STATE
- `PB13` <- P0 profile 选择按钮，低电平按下有效，内部上拉
- `PB6 / PB7` -> OLED（`I2C1`）
- `PC13` -> 板载状态灯
- `PA13 / PA14` -> SWD 下载调试

---

## 11. CubeMX 配置要点

当前 E1 工程建议保持这些前提：

- `TIM4_CH3` on `PB8` for DShot output
- `USART1` enabled for HC-05 serial
- `PB12` configured as Bluetooth state input
- `PB13` configured as button input with pull-up
- `I2C1` on `PB6 / PB7`
- `ADC1` with 2-channel scan + DMA circular

### 最关键的时序要求

- `TIM4` 必须保持 DShot 配置（`PSC=0`, `ARR=239`）

### 另外还要确保

- `DMA1_Channel5` interrupt is enabled for TIM4 CH3 DMA

---

## 12. 关键可调宏

定义位置：[`Core/Inc/app_e1_test.h`](./Core/Inc/app_e1_test.h)

### 时序

- `E1_BT_PREPARE_MS`
- `E1_BT_CONNECT_CONFIRM_MS`
- `E1_RAMP_MS`
- `E1_RAMP_START_HOLD_MS`
- `E1_RUN_MS`
- `E1_TEST_CYCLE_COUNT`
- `E1_REST_MS`
- `E1_DONE_REARM_DELAY_MS`
- `E1_SESSION_MAX_MS`
- `E1_CSV_INTERVAL_MS`

### 串口 / 蓝牙稳健性

- `E1_UART_BOOT_QUIET_MS`
- `E1_UART_TX_BUFFER_SIZE`

### DShot / 油门

- `E1_RUN_THROTTLE_DSHOT`
- `E1_RUN_THROTTLE_MIN_DSHOT`
- `E1_RUN_THROTTLE_MAX_DSHOT`
- `E1_P0_SI_R1_DSHOT` / `E1_P0_SI_R2_DSHOT` / `E1_P0_SI_R3_DSHOT`
- `E1_P0_GAN_R1_DSHOT` / `E1_P0_GAN_R2_DSHOT` / `E1_P0_GAN_R3_DSHOT`
- `E1_RAMP_START_DSHOT`
- `E1_DSHOT_SEND_INTERVAL_MS`

### 模拟量

- `VBAT_DIVIDER_RATIO`
- `INA199_GAIN_V_V`
- `CURRENT_SHUNT_RESISTANCE_OHM`
- `CURRENT_SCALE_A_PER_V`
- `E1_CURRENT_SIGN_INVERT`
- `E1_ZERO_OFFSET_SAMPLE_INTERVAL_MS`
- `E1_BASELINE_SAMPLE_COUNT`
- `E1_CURRENT_FILTER_SAMPLE_INTERVAL_MS`
- `E1_CURRENT_FILTER_WINDOW_MS`

### 安全保护

- `E1_CURRENT_TRIP_A`
- `E1_CURRENT_TRIP_HOLD_MS`
- `E1_CURRENT_FAST_TRIP_A`
- `E1_CURRENT_FAST_TRIP_HOLD_MS`

### 显示

- `E1_OLED_UPDATE_INTERVAL_MS`
- `E1_OLED_I2C_TIMEOUT_MS`
- `E1_OLED_POWERUP_DELAY_MS`
- `E1_OLED_RETRY_INTERVAL_MS`
- `E1_OLED_I2C_ADDR`
- `E1_OLED_COLUMN_OFFSET`

---

## 13. 编译

### 编译示例

```bash
cmake --preset Debug --fresh
cmake --build --preset Debug
```

### 产物包括

- `build/Debug/STM32_ESC_DSHOT_E1.elf`
- `build/Debug/STM32_ESC_DSHOT_E1.hex`
- `build/Debug/STM32_ESC_DSHOT_E1.bin`

---

## 14. 当前版本边界

这个仓库当前对应的是已经稳定下来的 E1 稳态实验版本。

它主要适用于：

- 已写入固件的 P0 同 RPM profile 工况测试
- 每个选定 profile 的 `RUN` 窗口内使用固定 DShot 命令
- 电流 / 电压 / 功率记录
- OLED 辅助台架操作
- 蓝牙触发实验开始

它**不包含**：

- RPM 闭环控制
- H2 阶跃响应流程以及后续实验

---

## 15. E1-V2.0-DShot300 版本更新

这次更新将油门协议时序从 `DShot500` 调整为 `DShot300`，使实验条件更接近日常飞行中常见的设置。

同时加入了 3 个更适合台架使用的改动：

- OLED 增加了上电延时初始化和后台重试机制，不再需要手动按复位后才亮屏
- `power_W` 现在直接按 `vbat_V * current_A` 计算
- 电流结果小于 `0` 时会强制钳到 `0`，避免稳态日志中出现负电流和负功率

这一版可作为当前稳态 ESC 对比流程的 `E1-V2.0-DShot300` 里程碑版本。

---

## 16. E1-V3.0-DShot300 版本更新

这一版继续使用 `DShot300`，重点优化高油门台架测试时的安全性和串口稳定性。

主要改动：

- 已写入固件的 6 个 P0 同 RPM profile 可在 `START` 前通过按钮选择
- 自动执行 3 轮测试流程：每轮先缓坡、再按选定 profile 命令运行 60 秒，轮次之间休息
- 软启动油门缓坡：每轮从 `E1_RAMP_START_DSHOT = 0` 开始，在 `E1_RAMP_MS = 3000` 内推到选定 P0 profile 命令
- 串口 CSV 输出间隔调整为 `E1_CSV_INTERVAL_MS = 1000`，降低 VOFA / 蓝牙缓存压力
- UART 输出改为非阻塞，新测试开始时会清掉旧的发送队列
- 加固蓝牙复位 / 重连行为，避免 MCU reset 后主动刷屏导致 VOFA 卡死
- `RAMP` 和 `RUN` 阶段都启用过流锁存保护和强制 DShot stop
- 快速过流检测使用显示/日志平滑窗口之前的瞬时 ADC 均值
- OLED I2C 超时按 100 kHz 总线重新调整，兼顾正常刷新和异常时不长时间卡主循环

当前 P0 profile 表使用 Si 命令 `551 / 786 / 1124` 和 GaN 命令 `540 / 762 / 1022`，对应 `3000 / 6500 / 10000 RPM` 目标点。

---

## 17. E1-V4.0-DShot300 版本更新

这一版继续使用 `DShot300`，并将固件、OLED 界面和 README 对齐到当前 P0 同 RPM E1 流程。

主要改动：

- 已写入固件的 6 个 P0 同 RPM profile 覆盖 Si 和 GaN 两块板子
- 蓝牙确认后、`START` 前，可通过 PB13 按钮循环选择 profile
- 自动执行 3 轮本地测试，每轮使用选定 profile 命令，并在 60 秒 `RUN` 前从 `0` 缓坡 3 秒
- README 中同步当前 `STOP` / `ABORT`、安全锁存和测试后 re-arm 行为
- OLED 5x7 字库扩展到大写 `A-Z`，`GaN`、`RUN`、`DONE` 等标签可正常显示
- README 表述统一为同 RPM profile 对比

---

## 18. E1-Final-DShot300 最终版

这一版标记 E1 实验阶段已经完成，并归档最终同 RPM 数据采集所使用的固件。

主要说明：

- 当前固件仍是 DShot300 P0 同 RPM profile 流程，并自动执行 3 轮测试
- 每轮先直接打到 DShot `500`，保持 `E1_RAMP_START_HOLD_MS = 250 ms`，并在 `E1_RAMP_MS = 5000` 内缓坡到选定 profile 命令
- 已弃用的 PB8 ESC 信号重连分支不会进入 `main`，也不属于此 release
- README 和项目交接说明已同步为最终归档状态

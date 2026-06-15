# AM32-MultiRotor-ESC-firmware

Firmware for ARM based speed controllers

<p align="left">
  <a href="/LICENSE">
    <img src="https://img.shields.io/badge/license-GPL--3.0-brightgreen" alt="GitHub license" />
  </a>
</p>

---

## FYP Modified Version

## FYP 修改版说明

This repository is a modified AM32 firmware workspace prepared for my Final Year Project (FYP).  
这个仓库是我为毕业设计（FYP）整理和修改的 AM32 固件工作区。

The upstream AM32 project uses a bootloader-based memory layout and configurator-based firmware update flow.  
原始 AM32 项目使用带 Bootloader 的内存布局，以及通过 Configurator 进行固件更新的标准流程。

The standard AM32 layout is bootloader at `0x08000000`, main firmware at `0x08001000`, and EEPROM written separately to the target-specific EEPROM region.  
标准 AM32 布局是 Bootloader 位于 `0x08000000`，主固件位于 `0x08001000`，EEPROM 再单独写入目标板对应的 EEPROM 区域。

For this FYP branch, the AT32F421 build was intentionally converted to a direct-flash development layout so the firmware can be compiled, flashed, and debugged directly from VS Code and DAPLink.  
对于这个 FYP 分支，我把 AT32F421 构建有意改成了“直烧开发布局”，这样就可以直接从 VS Code 配合 DAPLink 编译、烧录和调试。

The following list records the important code and engineering changes present in this repository at the time it was prepared.  
下面这份列表记录了这个仓库在整理完成时所包含的重要代码和工程改动。

### Important Code and Engineering Changes

- Original local logic change: `FIXED_SPEED_MODE` is enabled in [`Src/main.c`](/C:/Users/123/Desktop/AM32-main/Src/main.c), so the firmware bypasses the normal input protocol path and runs with a fixed-speed control loop.  
- 原始本地逻辑改动：[`Src/main.c`](/C:/Users/123/Desktop/AM32-main/Src/main.c) 中启用了 `FIXED_SPEED_MODE`，因此固件会绕过正常输入协议路径，直接以固定转速控制环运行。

- Original local logic change: `FIXED_SPEED_MODE_RPM` is set to `10000`, which means the current branch is configured to target a fixed motor speed of about 10000 RPM during this mode.  
- 原始本地逻辑改动：`FIXED_SPEED_MODE_RPM` 被设置为 `10000`，也就是当前分支在该模式下目标转速大约为 10000 RPM。

- Original local debug override: inside [`Src/main.c`](/C:/Users/123/Desktop/AM32-main/Src/main.c), the firmware explicitly sets `eepromBuffer.motor_poles = 14;`.  
- 原始本地调试覆盖：在 [`Src/main.c`](/C:/Users/123/Desktop/AM32-main/Src/main.c) 中，固件被明确设置为 `eepromBuffer.motor_poles = 14;`。

- Original local debug override: inside [`Src/main.c`](/C:/Users/123/Desktop/AM32-main/Src/main.c), the firmware explicitly sets `eepromBuffer.motor_kv = 49;`.  
- 原始本地调试覆盖：在 [`Src/main.c`](/C:/Users/123/Desktop/AM32-main/Src/main.c) 中，固件被明确设置为 `eepromBuffer.motor_kv = 49;`。

- Original local debug override: inside [`Src/main.c`](/C:/Users/123/Desktop/AM32-main/Src/main.c), the firmware explicitly sets `eepromBuffer.bi_direction = 0;`, forcing the ESC into normal one-way aircraft ESC behavior.  
- 原始本地调试覆盖：在 [`Src/main.c`](/C:/Users/123/Desktop/AM32-main/Src/main.c) 中，固件被明确设置为 `eepromBuffer.bi_direction = 0;`，强制电调按普通单向航模电调方式工作。

- Build-system change: the GCC linker script for AT32F421 in [`Mcu/f421/AT32F421x6_FLASH.ld`](/C:/Users/123/Desktop/AM32-main/Mcu/f421/AT32F421x6_FLASH.ld) was changed from the standard AM32 application-at-`0x08001000` layout to a direct-flash application-at-`0x08000000` layout.  
- 构建系统改动：[`Mcu/f421/AT32F421x6_FLASH.ld`](/C:/Users/123/Desktop/AM32-main/Mcu/f421/AT32F421x6_FLASH.ld) 中 AT32F421 的 GCC 链接脚本，已经从标准 AM32 的“应用位于 `0x08001000`”布局改成了“应用直接位于 `0x08000000`”的直烧布局。

- Build-system alignment: the direct-flash GCC layout was made consistent with the existing Keil scatter file [`Mcu/f421/Am32.sct`](/C:/Users/123/Desktop/AM32-main/Mcu/f421/Am32.sct), so Keil and GCC now follow the same AT32F421 flash map in this branch.  
- 构建系统对齐：这个 GCC 直烧布局已经和现有的 Keil Scatter 文件 [`Mcu/f421/Am32.sct`](/C:/Users/123/Desktop/AM32-main/Mcu/f421/Am32.sct) 保持一致，因此在这个分支里，Keil 和 GCC 对 AT32F421 使用同一套 Flash 映射。

- Memory-map detail: in the current AT32F421 layout, program flash starts at `0x08000000`, EEPROM remains at `0x08007C00`, and the AM32 file-name region remains at `0x08007BE0`.  
- 内存映射细节：在当前 AT32F421 布局中，程序 Flash 从 `0x08000000` 开始，EEPROM 仍然位于 `0x08007C00`，AM32 文件名区域仍然位于 `0x08007BE0`。

- RAM safety adjustment: the stack top in [`Mcu/f421/AT32F421x6_FLASH.ld`](/C:/Users/123/Desktop/AM32-main/Mcu/f421/AT32F421x6_FLASH.ld) is kept at `0x20003C00`, leaving headroom below the full 16 KB RAM limit for stable direct-flash debugging.  
- RAM 安全调整：[`Mcu/f421/AT32F421x6_FLASH.ld`](/C:/Users/123/Desktop/AM32-main/Mcu/f421/AT32F421x6_FLASH.ld) 中的栈顶被保持在 `0x20003C00`，没有顶到完整 16 KB RAM 上限，以便更稳定地进行直烧调试。

- VS Code engineering change: the repository now contains a working VS Code build / flash / debug workflow for AT32F421 + DAPLink.  
- VS Code 工程化改动：仓库现在包含一套可工作的 VS Code 构建 / 烧录 / 调试流程，面向 AT32F421 + DAPLink。

- VS Code engineering files added or updated: [`tasks.json`](/C:/Users/123/Desktop/AM32-main/.vscode/tasks.json), [`launch.json`](/C:/Users/123/Desktop/AM32-main/.vscode/launch.json), [`settings.json`](/C:/Users/123/Desktop/AM32-main/.vscode/settings.json), and [`extensions.json`](/C:/Users/123/Desktop/AM32-main/.vscode/extensions.json).  
- VS Code 工程文件新增或更新：[`tasks.json`](/C:/Users/123/Desktop/AM32-main/.vscode/tasks.json)、[`launch.json`](/C:/Users/123/Desktop/AM32-main/.vscode/launch.json)、[`settings.json`](/C:/Users/123/Desktop/AM32-main/.vscode/settings.json) 和 [`extensions.json`](/C:/Users/123/Desktop/AM32-main/.vscode/extensions.json)。

- Flash/debug integration change: OpenOCD support for DAPLink + AT32F421 was added through [`tools/openocd-at32f421-daplink.cfg`](/C:/Users/123/Desktop/AM32-main/tools/openocd-at32f421-daplink.cfg).  
- 烧录/调试集成改动：通过 [`tools/openocd-at32f421-daplink.cfg`](/C:/Users/123/Desktop/AM32-main/tools/openocd-at32f421-daplink.cfg) 新增了 DAPLink + AT32F421 的 OpenOCD 支持。

- Probe stability change: the DAPLink OpenOCD configuration uses the CMSIS-DAP HID backend and a conservative `100 kHz` SWD clock because this setting proved stable during actual flashing and debugging.  
- 探针稳定性改动：DAPLink 的 OpenOCD 配置使用了 CMSIS-DAP HID 后端，并把 SWD 时钟设置为较保守的 `100 kHz`，因为这个设置在实际烧录和调试中验证是稳定的。

- Debugging workflow change: the green Run/Debug action in VS Code is configured to build the firmware first, flash it next, and then enter a live debug session automatically.  
- 调试工作流改动：VS Code 中的绿色 Run/Debug 按钮现在被配置为先编译固件，再烧录，最后自动进入实时调试会话。

- Named debug configuration added: `AM32 DAPLink - Stop At main` runs to the start of `main`, which is useful for repeatable demonstrations and screenshots.  
- 新增命名调试配置：`AM32 DAPLink - Stop At main` 会停在 `main` 的开头，适合重复演示和截图记录。

- Named debug configuration added: `AM32 DAPLink - Stop Before Periph Init` halts before peripheral initialization, which is useful when explaining early startup behavior in a presentation or thesis.  
- 新增命名调试配置：`AM32 DAPLink - Stop Before Periph Init` 会在外设初始化前停下，适合在 PPT 或论文中讲解早期启动流程。

- Reproducibility note: the current VS Code settings contain machine-specific tool paths for the development PC used in this project, so another computer may need path adjustments before the workflow can be reused directly.  
- 可复现性说明：当前 VS Code 设置中包含了本项目开发电脑使用的本机工具路径，因此换一台电脑时，通常需要先调整路径后才能直接复用这套流程。

---

### Version `v0.3.0` Updates
### `v0.3.0` 版本更新

- Firmware change: the firmware returns to normal input-driven operation by disabling `FIXED_SPEED_MODE` in the source, while the fixed-speed test path remains in the codebase in a disabled state for later experiments.  
- `v0.3.0` 版本改动：通过在源码中禁用 `FIXED_SPEED_MODE`，固件回到正常的输入驱动运行，同时将固定转速测试路径以禁用状态保留在代码中，便于后续实验。  

- Runtime override change: the default FYP runtime overrides in `main.c` now include `motor_poles = 14`, encoded `motor_kv = 49`, runtime `motor_kv = 1960`, `bi_direction = 0`, `comp_pwm = 1`, `variable_pwm = 1`, `auto_advance = 0`, and `advance_level = 34` for a fixed 22.5 degree timing advance.  
- `v0.3.0` 版本改动：`main.c` 中的 FYP 默认运行覆盖现在包括 `motor_poles = 14`、编码后的 `motor_kv = 49`、运行时 `motor_kv = 1960`、`bi_direction = 0`、`comp_pwm = 1`、`variable_pwm = 1`、`auto_advance = 0` 以及 `advance_level = 34`，用于实现固定 22.5 度的进角。  

- Startup tuning change: the startup defaults were retuned for bench-supply testing by enabling sinusoidal startup, setting `startup_power = 60`, `sine_mode_changeover_thottle_level = 20`, `sine_mode_power = 4`, and keeping `stall_protection = 0`.  
- `v0.3.0` 版本改动：为了配合台式电源测试，启动默认参数被重新调整为更柔和的组合，其中包括启用正弦波启动，设置 `startup_power = 60`、`sine_mode_changeover_thottle_level = 20`、`sine_mode_power = 4`，并保持 `stall_protection = 0`。  

- Safety-path change: the fixed-speed-mode safety path was hardened so staged fixed-RPM testing starts from an all-off bridge state, ignores floating input-pin reset behavior, and fully disables PWM plus bridge drive when the timed sequence ends.  
- `v0.3.0` 版本改动：固定转速模式的安全路径被进一步加固，使分段定转速测试从桥臂全关断状态开始，忽略悬空输入脚引起的复位行为，并在计时序列结束时完全关闭 PWM 与桥臂驱动。  

- Driver fix: the F421 `proportionalBrake()` implementation was corrected by fixing the low-side GPIO port selection for phase B PWM braking.  
- `v0.3.0` 版本改动：F421 的 `proportionalBrake()` 实现已经被修正，通过修正 B 相低边 PWM 制动时的 GPIO 端口选择完成。  

---

### Version `v0.5.0` Updates
### `v0.5.0` 版本更新

- Refactor change: the manual ESC parameter override block was moved out of `main.c` and centralized into `Inc/ESC_Settings.h` so all FYP tuning values can be maintained in one place.  
- `v0.5.0` 版本改动：手动 ESC 参数覆盖代码块已经从 `main.c` 中抽离，并集中整理到 `Inc/ESC_Settings.h` 中，方便将 FYP 调参内容统一维护在一个位置。  

- Maintainability change: `main()` now applies the whole custom ESC parameter set through a single `ApplyCustomEscSettings(...)` call instead of a long inline block.  
- `v0.5.0` 版本改动：`main()` 现在通过一次 `ApplyCustomEscSettings(...)` 调用统一应用整套自定义 ESC 参数，不再保留一大段内联参数赋值代码。  

- Documentation change: English comments were added for every custom ESC setting macro so the parameter file can also serve as a compact configuration reference.  
- `v0.5.0` 版本改动：已经为每一个自定义 ESC 参数宏补充英文注释，使该参数文件同时具备简明配置说明的作用。  

- Parameter-set continuity: the centralized settings keep the existing FYP values, including 14 poles, encoded KV 49, runtime KV 1960, fixed 22.5-degree timing advance, 48kHz PWM, sinusoidal startup, and both brake levels set to 10.  
- `v0.5.0` 版本改动：集中后的参数文件保留了现有 FYP 参数，包括 14 极、编码 KV 值 49、运行时 KV 1960、固定 22.5 度进角、48kHz PWM、正弦启动，以及两级刹车强度均设为 10。  

---

- citation sentence: this repository is not a pure mirror of upstream AM32, but a research-development branch customized for AT32F421 direct flashing, debugging, and fixed-speed experimental validation.  
- 概括：这个仓库并不是 AM32 的纯镜像，而是一个为了 AT32F421 直烧、调试和固定转速实验验证而定制的研究开发分支。

---

The AM32 firmware is designed for STM32 ARM processors to control a brushless motor (BLDC).  
The firmware is intended to be safe and fast with smooth fast startups and linear throttle. It is meant for use with multiple vehicle types and a flight controller. The firmware can also be built with support for crawlers. For crawler usage please read this wiki page [Crawler Hardware](https://github.com/AlkaMotors/AM32-MultiRotor-ESC-firmware/wiki/Crawler-Hardware-and-AM32)

## Features

AM32 has the following features:

- Firmware upgradable via betaflight passthrough, single wire serial or arduino
- Servo PWM, Dshot(300, 600) motor protocol support
- Bi-directional Dshot
- KISS standard ESC telemetry
- Variable PWM frequency
- Sinusoidal startup mode, which is designed to get larger motors up to speed

---

## Build instructions

Download and install Keil community edition. Open the Keil project for the mcu you want in the "Keil projects" folder. Install any mcu packs if prompted.  
Select the build target from the drop down box and build project

---

## Firmware Release & Configuration Tool

The latest release of the firmware can be found [here](https://am32.ca/downloads).

The primary configurator is the [AM32 Configurator](https://am32.ca)  
which supports web browser based configuration and firmware update.

You can also use a desktop configurator which you can download from here:

- [WINDOWS](https://drive.google.com/file/d/16kaPek9umz7fQFunzBeW4pp2LgT6_E5o/view?usp=drive_link)
- [LINUX](https://drive.google.com/file/d/1QtSKwp3RT6sncPADsPkmdasGqNIk68HH/view?usp=sharing)

Alternately you can use the [Online-ESC Configurator](https://esc-configurator.com/) to flash or change settings with any web browser that supports web serial.

---

## Hardware

AM32 currently has support for STSPIN32F0, STM32F051, STM32G071, GD32E230, AT32F415 and AT32F421.  
The CKS32F051 is not recommended due to too many random issues.  
Target compatibility List can be found [here](https://github.com/am32-firmware/AM32/blob/main/Inc/targets.h)

---

## Installation & Bootloader

To use AM32 firmware on a blank ESC, a bootloader must first be installed using an ST-LINK, GD-LINK , CMIS-DAP or AT-LINK. THe bootloader will be dependant on the MCU used ont he esc . Choose the bootloader that matches the MCU type and signal input pin of the ESC.  
The compatibility chart has the bootloader pinouts listed.  
Current bootloaders can be found [here](https://github.com/am32-firmware/AM32-bootloader).

After the bootloader has been installed the main firmware from can be installed either with the configuration tools and a Betaflight flight controller or a direct connection with a usb serial adapter modified for one wire.

To update an existing AM32 bootloader an update tool can be found [here](https://github.com/am32-firmware/AM32-unlocker).

---

## Support and Developers Channel

There are two ways you can get support or participate in improving am32.  
We have a discord server here:

https://discord.gg/h7ddYMmEVV

Etiquette: Please wait around long enough for a reply - sometimes people are out flying, asleep or at work and can't answer immediately.

If you wish to support the project please join the Patreon.

https://www.patreon.com/user?u=44228479

---

## Sponsors

The AM32 project would not have made this far without help from the following sponsors:

- Holmes Hobbies - https://holmeshobbies.com/ - The project would not be where it is today without the support of HH. Check out the Crawlmaster V2 for the best am32 experience!
- Repeat Robotics - https://repeat-robotics.com/ - Bringing Am32 esc's to the fighting robot community!
- Quaternium - https://www.quaternium.com/ - Firmware development support and hardware donations
- Airbot - Many hardware donations
- NeutronRC - For hardware, am32 promotion and schematics
- Aikon - Hardware donations and schematics
- Skystars - For hardware and taking a chance on the first commercial am32 esc's
- Diatone - Hardware donations
- T-motor - Motor and Hardware donations
- HLGRC - Hardaware donations

---

## Contributors

A big thanks to all those who contributed time, advice and code to the AM32 project.

- Un!t
- Hugo Chiang (Dusking)
- Micheal Keller (Mikeller)
- ColinNiu
- Jacob Walser

And for feedback from pilots and drivers:

- Jye Smith
- Markus Gritsch
- Voodoobrew

(and many more)

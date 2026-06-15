# P0 DShot-RPM 映射固件中文使用说明

本文说明如何使用 `STM32_ESC_DSHOT_P0_RPM_MAPPING` 固件做 P0 预实验：给 ESC 发送固定原始 DShot 命令，手动用激光转速计测 RPM，并把读数记录在纸上或外部表格里，最后人工汇总 DShot-to-RPM 映射。

重要限制：这个固件不会自己测量 RPM。默认也不再要求通过串口输入 RPM；RPM 由手持激光转速计测量后外部记录。

## 1. 这个固件用来做什么

P0 的目标是为后续同转速 E1/E2/E3 实验做准备。

你需要分别对 Si ESC 和 GaN ESC 运行同一套 DShot 命令扫描，得到两张映射表：

- `RPM_mapping_Si.csv`
- `RPM_mapping_GaN.csv`

之后从两张表里选择双方都能稳定达到、RPM 尽量接近的 R1/R2/R3 三个转速点，再用于后续同 RPM 对比实验。

P0 只负责：

- 保持指定 DShot 命令
- 记录电压、电流、功率
- 提示你当前正在测哪个 DShot 命令
- 输出可解析的 summary 日志

P0 不负责：

- RPM 闭环 PID 控制
- 自动测 RPM
- 证明效率结论

## 2. 硬件连接

保持 E1 控制器硬件接线不变：

| STM32 引脚 | 连接对象 | 用途 |
| --- | --- | --- |
| `PB8` | ESC signal | DShot 输出，`TIM4_CH3` |
| `PA0` | INA199 输出 | 电流采样，`ADC1_IN0` |
| `PA1` | 电池分压中点 | 电压采样，`ADC1_IN1` |
| `PA9` | HC-05 RXD | `USART1_TX` |
| `PA10` | HC-05 TXD | `USART1_RX` |
| `PB12` | HC-05 STATE | 蓝牙连接状态输入 |
| `PB13` | 本地按钮 | 低电平按下，内部上拉 |
| `PB6` | OLED SCL | `I2C1_SCL` |
| `PB7` | OLED SDA | `I2C1_SDA` |
| `PC13` | 板载 LED | 状态指示 |
| `PA13/PA14` | SWD | 下载和调试 |

默认固件要求 HC-05 蓝牙连接确认：

- `P0_REQUIRE_BT = 1`
- HC-05 `STATE` 稳定为高电平后进入 `READY`

如果你没有接 HC-05 STATE，固件会停在 `WAIT_BT`。这种情况下要么接好 HC-05，要么在编译前把 `Core/Inc/app_p0_rpm_map.h` 里的 `P0_REQUIRE_BT` 改为 `0`。

## 3. 编译和烧录

在仓库根目录运行：

```bash
cmake --preset Debug --fresh
cmake --build --preset Debug
```

成功后会生成：

```text
build/Debug/STM32_ESC_DSHOT_P0_RPM_MAPPING.elf
build/Debug/STM32_ESC_DSHOT_P0_RPM_MAPPING.hex
build/Debug/STM32_ESC_DSHOT_P0_RPM_MAPPING.bin
```

用 STM32CubeProgrammer 或 VS Code 任务烧录 `.hex`：

```text
build/Debug/STM32_ESC_DSHOT_P0_RPM_MAPPING.hex
```

VS Code 中可用：

- `STM32: Build Debug`
- `STM32: Flash Debug Hex`
- `STM32: Build + Flash`

## 4. 串口设置

HC-05 串口参数：

```text
9600 baud
8 data bits
no parity
1 stop bit
```

推荐用能保存日志的串口工具，例如 VOFA、PuTTY、MobaXterm、Tera Term 或 Arduino Serial Monitor。

每条命令可以用 `\r`、`\n` 或 `\r\n` 结尾。命令大小写不敏感。

上电并连接蓝牙后，你应该看到类似输出：

```text
# boot app=P0_RPM_MAPPING version=0.1 uart=9600 protocol=firewater
# connected
# ready board=UNKNOWN load=prop sweep_count=11
```

## 5. 常用命令

查看帮助：

```text
HELP
```

查看状态：

```text
STATUS
```

设置 ESC 类型：

```text
BOARD Si
BOARD GaN
```

负载模式默认是带桨 `prop`。正常带桨测试时不需要设置它；下面命令只保留给特殊标记用途：

```text
LOAD prop
LOAD noProp
```

开始实验：

```text
START
```

测完当前油门点并手写记录 RPM 后，进入下一个点：

```text
NEXT
```

重复当前命令点：

```text
REPEAT
```

停止：

```text
STOP
ABORT
```

## 6. Sweep 设置

默认扫描命令是：

```text
600, 700, 800, 900, 1000, 1100, 1200, 1300, 1400, 1500, 1600
```

设置自定义扫描：

```text
SWEEP 600 1600 100
```

只跑单个命令点：

```text
CMD 900
START
```

启用高命令扩展：

```text
EXTEND ON
```

启用后默认扫描会变成 `600..1800`，步长仍是 `100`。

关闭扩展：

```text
EXTEND OFF
```

注意：`1700` 和 `1800` 默认不会自动运行，必须通过 `EXTEND ON`、`SWEEP` 或 `CMD` 明确要求。

## 7. 一次完整 Si ESC 实验流程

1. 安装 Si ESC，保持电机、桨、供电和机械固定方式一致。
2. 打开串口日志保存。
3. 连接 HC-05，等待固件进入 `READY`。
4. 输入：

```text
BOARD Si
SWEEP 600 1600 100
START
```

5. 固件每次 `START` 都会先进入 `PREPARE`，此时电机停止，等待倒计时结束。
6. 第一个命令点会进入稳定和保持阶段：

```text
STABILIZE -> MEASURE
```

默认 `P0_REQUIRE_NEXT_TO_ADVANCE=1`，固件不会因为计时结束而自动停机或进入下一个点。当前点会一直保持目标 DShot 命令，直到你发送 `NEXT`。手动模式下，`NEXT` 和 `REPEAT` 只有在进入 `MEASURE`、串口已经输出 `# throttle_hold ...` 之后才会生效；如果串口工具重复发送命令，或者你在 `RAMP` / `STABILIZE` 阶段误发，固件会忽略。发送有效 `NEXT` 后，固件会直接从当前命令跳到下一个命令，例如 `600 -> 700`，中间不会发 DShot `0` 停转。

默认 `P0_JUMP_TO_TARGET_FROM_STOP=1`，第一点会直接给 `600`，让 A32 电调自己的启动逻辑接管。若编译前把这个宏改成 `0`，从停机进入第一点时会恢复线性爬坡。

如果一次实验从停机启动，且第一个目标油门大于 `800`，例如 `CMD 900` 或 `SWEEP 1000 1600 100`，固件会自动走高油门保护爬坡：先给 `600`，再用默认 `6000 ms` 从 `600` 爬到目标油门。正常 `SWEEP 600 1600 100` 不受影响，仍然第一点直接给 `600`；已经转起来以后发送 `NEXT` 时，也仍然是从上一个油门直接跳到下一个油门，不会停转。触发保护爬坡时，串口会先输出 `# p0_ramp_start ... from=600 to=<目标油门> ms=6000`。

7. 串口出现 `# throttle_hold ... action=measure_on_paper_send_NEXT` 时，用激光转速计测 RPM，并把 RPM 手写记录在 A4 纸或表格里。默认运行时 OLED 不刷新，以减少 I2C 对 DShot 发送节奏的干扰。

8. 当前点测完后，输入：

```text
NEXT
```

9. 每个点结束时，串口会输出类似：

```text
# p0_step_summary date=NA board=Si load_mode=prop index=0 step_index=0 cmd=600 dshot_cmd=600 rpm1=0 rpm2=0 rpm3=0 rpm_used=0 rpm_mean=0.0 rpm_min=0 rpm_max=0 rpm_range=0 vbat_mean_V=14.800 current_mean_A=1.230 power_mean_W=18.204 status=throttle_only
```

10. 全部点完成后，固件输出：

```text
# p0_done board=Si load=prop points=11
```

11. 保存串口日志。

## 8. 一次完整 GaN ESC 实验流程

换成 GaN ESC 后，保持其余条件不变。重新上电或输入 `START` 前确认状态在 `READY` / `DONE` / `ABORTED`。

输入：

```text
BOARD GaN
SWEEP 600 1600 100
START
```

之后按同样方式测量、手写记录每个命令点的 RPM，并用 `NEXT` 切到下一个点。

最终你应该得到一份 GaN 日志，用于生成 `RPM_mapping_GaN.csv`。

## 9. OLED 显示含义

OLED 在电机停止或 `PREPARE` 阶段会显示五行：

```text
VBAT 14.720V
CURR 1.340A
POWR 19.728W
P0 CMD0800
PREP 03S
```

默认 `P0_OLED_UPDATE_WHILE_ACTIVE=0`，进入 `RAMP` / `STABILIZE` / `MEASURE` 后不会继续刷新 OLED。这样做是为了避免 100kHz I2C 阻塞刷屏打断 DShot 连续帧，尤其是 1200 以上高油门测试时。运行中请以串口里的 `# p0_step_start`、`# throttle_hold`、`# fault`、`# abort` 作为可靠状态记录。

如果以后确实需要运行中实时刷新 OLED，可以编译前把 `P0_OLED_UPDATE_WHILE_ACTIVE` 改成 `1`，但高油门测试时不建议这么做。

第 5 行状态含义：

| 显示 | 含义 |
| --- | --- |
| `WAIT BT` | 等待 HC-05 连接 |
| `READY` | 就绪，电机停止 |
| `PREP xxS` | 开始前安全倒计时 |
| `RAMP xxS` | 编译为线性爬坡模式时，正在升到目标 DShot 命令 |
| `STAB xxS` | 保持命令，等待转速稳定 |
| `HOLD NEXT` | 若启用运行中 OLED 刷新，则表示保持当前油门，等待你手写记录后发送 `NEXT` |
| `REST xxS` | 旧版定时步进模式下的停机休息 |
| `DONE` | 扫描完成 |
| `ABORT` | 用户停止 |
| `FAULT OC` | 故障停机 |

## 10. 按钮行为

`PB13` 按钮为低电平按下。

在 `READY` / `DONE` / `ABORTED`：

- 短按：切换 OLED 上显示的预览命令
- 长按：开始 mapping

在 `RAMP` / `STABILIZE` / `MEASURE`：

- 任意按下：立即停止，进入 `ABORTED`

默认不通过按钮或串口输入 RPM；RPM 建议用纸笔或外部表格记录。

## 11. 日志和 CSV

固件会持续输出 `p0:` 数据帧，适合 VOFA/FireWater 记录：

```text
p0:t_ms,state,step_index,cmd_raw,dshot_cmd,adc_i_raw,adc_vbat_raw,v_i_sense,v_vbat_adc,current_A,vbat_V,power_W,zero_offset_V,active_zero_offset_V,delta_i_V,rpm_count,rpm1,rpm2,rpm3,rpm_used,flags
```

每个点结束时会输出 summary，方便把串口时间、电压、电流、功率和你纸上记录的 RPM 对齐：

```text
# p0_step_summary ...
```

如果仍想从串口日志提取每个油门点的 summary，可以转换成 CSV：

```bash
python -B tools/parse_p0_log.py --input 2026-05-12_P0_Si_mapping_prop.txt --output RPM_mapping_Si.csv
python -B tools/parse_p0_log.py --input 2026-05-12_P0_GaN_mapping_prop.txt --output RPM_mapping_GaN.csv
```

CSV 字段：

```text
date,board,load_mode,step_index,dshot_cmd,rpm1,rpm2,rpm3,rpm_used,rpm_mean,rpm_min,rpm_max,rpm_range,vbat_mean_V,current_mean_A,power_mean_W,status
```

默认纸笔记录模式下，CSV 里的 `rpm1/rpm2/rpm3/rpm_used` 会是 `0`，`status` 会是 `throttle_only`。后续汇总时，把手写 RPM 按 `step_index` / `dshot_cmd` 填回表格即可。

## 12. 安全注意事项

台架测试有风险，请先确认：

- 电机和桨固定牢靠
- 人不要站在桨平面内
- 电源限流合理
- ESC 和电机散热足够
- 串口日志已经开始保存
- 手边有断电手段

固件内置保护：

- `STOP` / `ABORT` 会立即发送 DShot `0`
- 运行中按按钮会立即停机
- 过流保护默认开启：`P0_CURRENT_TRIP_A = 8.0f`
- 欠压保护宏存在，但默认关闭：`P0_ENABLE_VBAT_TRIP = 0`

`P0_CURRENT_TRIP_A` 是保守占位值，必须按你的实际台架、电源、电机、桨和 ESC 能力重新确认。

## 13. 常见问题

看不到串口输出：

- 确认 HC-05 已连接
- 确认 `PB12` 的 HC-05 `STATE` 为高电平
- 确认串口是 `9600 8N1`
- 如果没有接 HC-05 STATE，把 `P0_REQUIRE_BT` 改为 `0` 后重新编译

输入 `RPM 9000` 没反应：

- 默认 `P0_ENABLE_UART_RPM_INPUT = 0`，串口 RPM 输入已关闭
- 当前流程是测完后手写记录，然后发送 `NEXT`

输入 `NEXT` 后 summary 显示 `throttle_only`：

- 这是默认纸笔记录模式的正常状态
- 串口日志只记录油门点、电压、电流、功率；RPM 之后人工汇总

想只测一个命令：

```text
CMD 900
START
```

想紧急停机：

```text
STOP
```

或直接按下 `PB13` 按钮。

## 14. 建议记录命名

建议用清晰文件名保存串口日志：

```text
2026-05-12_P0_Si_mapping_prop.txt
2026-05-12_P0_GaN_mapping_prop.txt
```

然后转换成：

```text
RPM_mapping_Si.csv
RPM_mapping_GaN.csv
```

后续选择 R1/R2/R3 时，把纸笔记录汇总后的 Si 和 GaN RPM 按接近程度对齐即可。

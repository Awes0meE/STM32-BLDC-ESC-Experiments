#pragma once

#include <stdint.h>

#include "common.h"
#include "functions.h"
#include "peripherals.h"
#include "targets.h"

// Motor pole count used by the ESC control logic.
#define ESC_SETTING_MOTOR_POLES 14U
// EEPROM-encoded motor KV value, where 49 is the closest code for a 1960KV motor.
#define ESC_SETTING_MOTOR_KV_CODE 49U
// Runtime motor KV value used directly in control calculations.
#define ESC_SETTING_MOTOR_KV_RUNTIME 1960U

// Disable bidirectional forward/reverse mode.
#define ESC_SETTING_BI_DIRECTION 0U
// Enable complementary PWM output.
#define ESC_SETTING_COMP_PWM 1U
// Disable variable PWM and keep a fixed PWM frequency.
#define ESC_SETTING_VARIABLE_PWM 0U
// Disable auto timing advance and use a fixed advance value.
#define ESC_SETTING_AUTO_ADVANCE 0U
// Stored timing advance setting corresponding to 22.5 degrees.
#define ESC_SETTING_ADVANCE_LEVEL 34U
// Runtime timing advance step value corresponding to 22.5 degrees.
#define ESC_SETTING_ADVANCE_RUNTIME 24U
// PWM switching frequency in kHz.
#define ESC_SETTING_PWM_FREQUENCY 48U
// Enable sinusoidal startup for a softer motor launch.
#define ESC_SETTING_USE_SINE_START 1U
// Startup power level used during the initial spin-up phase.
#define ESC_SETTING_STARTUP_POWER 60U
// Throttle range threshold for switching out of sine startup mode.
#define ESC_SETTING_SINE_STARTUP_RANGE 20U
// Output power scaling used while running in sine mode.
#define ESC_SETTING_SINE_MODE_POWER 6U
// Enable stuck rotor protection to remove drive quickly if the motor locks.
#define ESC_SETTING_STUCK_ROTOR_PROTECTION 1U
// Disable stall protection to avoid extra low-speed throttle boost.
#define ESC_SETTING_STALL_PROTECTION 0U
// Disable RC car reverse mode so startup logic stays in aircraft-style behavior.
#define ESC_SETTING_RC_CAR_REVERSE 0U
// Disable brake on stop so the motor phases float at zero throttle.
#define ESC_SETTING_BRAKE_ON_STOP 0U
// Set active brake power to zero.
#define ESC_SETTING_ACTIVE_BRAKE_POWER 0U
// Stopped Brake Level used by the stopped drag brake path.
#define ESC_SETTING_STOPPED_BRAKE_LEVEL 10U
// Running Brake Level used by the running brake path.
#define ESC_SETTING_RUNNING_BRAKE_LEVEL 10U

static inline void ApplyCustomEscSettings(
    EEprom_t *settings,
    uint16_t *timer1_max_arr,
    uint16_t *tim1_arr,
    uint16_t *motor_kv_runtime,
    uint8_t *temp_advance_runtime,
    uint16_t minimum_duty_cycle_runtime,
    uint16_t *min_startup_duty_runtime,
    uint16_t *low_rpm_level_runtime,
    uint16_t *high_rpm_level_runtime,
    uint16_t *reverse_speed_threshold_runtime,
    volatile uint32_t *polling_mode_changeover_runtime) {

    /* ===== 电机参数修改 ===== */
    settings->motor_poles = ESC_SETTING_MOTOR_POLES; // 电机极数，当前电机为 14 极
    settings->motor_kv = ESC_SETTING_MOTOR_KV_CODE; // EEPROM 中的 KV 编码值，49 对应接近 1960KV 的档位

    /* ===== 电调参数修改 ===== */
    settings->bi_direction = ESC_SETTING_BI_DIRECTION; // 关闭双向模式，按普通单向航模电调运行
    settings->comp_pwm = ESC_SETTING_COMP_PWM; // 开启互补 PWM
    settings->variable_pwm = ESC_SETTING_VARIABLE_PWM; // 关闭变频 PWM，保持固定 PWM 频率输出
    settings->auto_advance = ESC_SETTING_AUTO_ADVANCE; // 关闭自动进角，使用固定进角
    settings->advance_level = ESC_SETTING_ADVANCE_LEVEL; // 固定 22.5 度进角，对应 temp_advance = 24
    settings->pwm_frequency = ESC_SETTING_PWM_FREQUENCY; // 设定 PWM 驱动频率为 48kHz
    settings->use_sine_start = ESC_SETTING_USE_SINE_START; // 开启正弦启动，减小硬拖启动电流
    settings->startup_power = ESC_SETTING_STARTUP_POWER; // 启动功率设为 60，减小启动瞬时冲击
    settings->sine_mode_changeover_thottle_level = ESC_SETTING_SINE_STARTUP_RANGE; // 正弦启动切换范围设为 20
    settings->sine_mode_power = ESC_SETTING_SINE_MODE_POWER; // 正弦模式功率设为 6
    settings->stuck_rotor_protection = ESC_SETTING_STUCK_ROTOR_PROTECTION; // 开启卡转保护，卡死时尽快撤驱动
    settings->stall_protection = ESC_SETTING_STALL_PROTECTION; // 关闭堵转保护，避免低速时额外补油
    settings->rc_car_reverse = ESC_SETTING_RC_CAR_REVERSE; // 关闭车模反转模式，避免改写启动策略
    settings->brake_on_stop = ESC_SETTING_BRAKE_ON_STOP; // 关闭停转刹车，停机时统一三相悬空
    settings->active_brake_power = ESC_SETTING_ACTIVE_BRAKE_POWER; // 主动刹车功率清零，避免停机时误注入制动占空比
    settings->drag_brake_strength = ESC_SETTING_STOPPED_BRAKE_LEVEL; // Stopped Brake Level 设为稳定值 10
    settings->driving_brake_strength = ESC_SETTING_RUNNING_BRAKE_LEVEL; // Running Brake Level 设为稳定值 10

    if (settings->pwm_frequency < 145U && settings->pwm_frequency > 7U) {
        uint32_t divider = settings->pwm_frequency * 100U / 6U; // 将 PWM 频率参数换算成定时器分频比例
        *timer1_max_arr = TIM1_AUTORELOAD * 400U / divider; // 重新计算当前 PWM 自动重装值
        *tim1_arr = *timer1_max_arr; // 同步更新当前 PWM ARR
        SET_AUTO_RELOAD_PWM(*timer1_max_arr); // 立即把新的 PWM 频率写入定时器
    }

    *motor_kv_runtime = ESC_SETTING_MOTOR_KV_RUNTIME; // 运行时电机 KV 按真实值 1960 参与控制计算
    *temp_advance_runtime = ESC_SETTING_ADVANCE_RUNTIME; // 运行时进角步进值，24 对应 22.5 度
    *min_startup_duty_runtime = minimum_duty_cycle_runtime + settings->startup_power; // 重新同步最小启动占空比
    *low_rpm_level_runtime = *motor_kv_runtime / 100U / (32U / settings->motor_poles); // 重新计算低转速分界
    *high_rpm_level_runtime = *motor_kv_runtime / 12U / (32U / settings->motor_poles); // 重新计算高转速分界
    *reverse_speed_threshold_runtime = (uint16_t)map(*motor_kv_runtime, 300, 3000, 1000, 500); // 重新计算反转切换阈值
    *polling_mode_changeover_runtime = POLLING_MODE_THRESHOLD; // 保持单向模式下的默认轮询切换阈值
}

#ifndef APP_P0_RPM_MAP_H
#define APP_P0_RPM_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#ifndef P0_REQUIRE_BT
#define P0_REQUIRE_BT                    1U
#endif

#ifndef P0_BT_CONNECT_CONFIRM_MS
#define P0_BT_CONNECT_CONFIRM_MS         500U
#endif

#ifndef P0_BT_LOSS_ABORT_MS
#define P0_BT_LOSS_ABORT_MS              1000U
#endif

#ifndef P0_PREPARE_MS
#define P0_PREPARE_MS                    10000U
#endif

#ifndef P0_RAMP_MS
#define P0_RAMP_MS                       4000U
#endif

#ifndef P0_HIGH_START_RAMP_THRESHOLD
#define P0_HIGH_START_RAMP_THRESHOLD     800U
#endif

#ifndef P0_HIGH_START_RAMP_START
#define P0_HIGH_START_RAMP_START         600U
#endif

#ifndef P0_HIGH_START_RAMP_MS
#define P0_HIGH_START_RAMP_MS            6000U
#endif

#ifndef P0_JUMP_TO_TARGET_FROM_STOP
#define P0_JUMP_TO_TARGET_FROM_STOP      1U
#endif

#ifndef P0_STABILIZE_MS
#define P0_STABILIZE_MS                  12000U
#endif

#ifndef P0_MEASURE_MS
#define P0_MEASURE_MS                    30000U
#endif

#ifndef P0_REQUIRE_NEXT_TO_ADVANCE
#define P0_REQUIRE_NEXT_TO_ADVANCE       1U
#endif

#ifndef P0_REST_MS
#define P0_REST_MS                       8000U
#endif

#ifndef P0_DSHOT_SEND_INTERVAL_MS
#define P0_DSHOT_SEND_INTERVAL_MS        2U
#endif

#ifndef P0_CSV_INTERVAL_MS
#define P0_CSV_INTERVAL_MS               500U
#endif

#ifndef P0_OLED_UPDATE_INTERVAL_MS
#define P0_OLED_UPDATE_INTERVAL_MS       200U
#endif

#ifndef P0_OLED_UPDATE_WHILE_ACTIVE
#define P0_OLED_UPDATE_WHILE_ACTIVE      0U
#endif

#ifndef P0_DEFAULT_SWEEP_START
#define P0_DEFAULT_SWEEP_START           600U
#endif

#ifndef P0_DEFAULT_SWEEP_END
#define P0_DEFAULT_SWEEP_END             1600U
#endif

#ifndef P0_DEFAULT_SWEEP_STEP
#define P0_DEFAULT_SWEEP_STEP            100U
#endif

#ifndef P0_EXTENDED_SWEEP_END
#define P0_EXTENDED_SWEEP_END            1800U
#endif

#ifndef P0_MAX_SWEEP_POINTS
#define P0_MAX_SWEEP_POINTS              32U
#endif

#ifndef P0_RPM_REQUIRED_READINGS
#define P0_RPM_REQUIRED_READINGS         3U
#endif

#ifndef P0_ENABLE_UART_RPM_INPUT
#define P0_ENABLE_UART_RPM_INPUT         0U
#endif

#ifndef P0_RPM_MAX_READINGS
#define P0_RPM_MAX_READINGS              8U
#endif

#ifndef P0_CURRENT_TRIP_A
#define P0_CURRENT_TRIP_A                8.0f
#endif

#ifndef P0_CURRENT_TRIP_HOLD_MS
#define P0_CURRENT_TRIP_HOLD_MS          300U
#endif

#ifndef P0_ENABLE_CURRENT_TRIP
#define P0_ENABLE_CURRENT_TRIP           1U
#endif

#ifndef P0_VBAT_MIN_V
#define P0_VBAT_MIN_V                    9.0f
#endif

#ifndef P0_ENABLE_VBAT_TRIP
#define P0_ENABLE_VBAT_TRIP              0U
#endif

#ifndef P0_CONTINUOUS_SWEEP
#define P0_CONTINUOUS_SWEEP              0U
#endif

#ifndef P0_DSHOT_MIN_THROTTLE
#define P0_DSHOT_MIN_THROTTLE            48U
#endif

#ifndef P0_DSHOT_MAX_THROTTLE
#define P0_DSHOT_MAX_THROTTLE            2047U
#endif

#ifndef P0_DSHOT_TELEMETRY_BIT
#define P0_DSHOT_TELEMETRY_BIT           0U
#endif

#ifndef P0_UART_BOOT_QUIET_MS
#define P0_UART_BOOT_QUIET_MS            1000U
#endif

#ifndef P0_UART_TX_BUFFER_SIZE
#define P0_UART_TX_BUFFER_SIZE           1536U
#endif

#ifndef P0_UART_CMD_BUFFER_SIZE
#define P0_UART_CMD_BUFFER_SIZE          80U
#endif

#ifndef P0_BUTTON_DEBOUNCE_MS
#define P0_BUTTON_DEBOUNCE_MS            30U
#endif

#ifndef P0_BUTTON_LONG_PRESS_MS
#define P0_BUTTON_LONG_PRESS_MS          1000U
#endif

#ifndef P0_OLED_I2C_TIMEOUT_MS
#define P0_OLED_I2C_TIMEOUT_MS           25U
#endif

#ifndef P0_OLED_POWERUP_DELAY_MS
#define P0_OLED_POWERUP_DELAY_MS         120U
#endif

#ifndef P0_OLED_RETRY_INTERVAL_MS
#define P0_OLED_RETRY_INTERVAL_MS        500U
#endif

#ifndef P0_OLED_I2C_ADDR
#define P0_OLED_I2C_ADDR                 (0x3CU << 1)
#endif

#ifndef P0_OLED_COLUMN_OFFSET
#define P0_OLED_COLUMN_OFFSET            2U
#endif

#ifndef ADC_REF_VOLTAGE
#define ADC_REF_VOLTAGE                  3.3f
#endif

#ifndef ADC_MAX_COUNTS
#define ADC_MAX_COUNTS                   4095.0f
#endif

#ifndef VBAT_DIVIDER_RATIO
#define VBAT_DIVIDER_RATIO               11.060484f
#endif

#ifndef CURRENT_OFFSET_V
#define CURRENT_OFFSET_V                 0.0f
#endif

#ifndef INA199_GAIN_V_V
#define INA199_GAIN_V_V                  200.0f
#endif

#ifndef CURRENT_SHUNT_RESISTANCE_OHM
#define CURRENT_SHUNT_RESISTANCE_OHM     0.00025f
#endif

#ifndef CURRENT_SCALE_A_PER_V
#define CURRENT_SCALE_A_PER_V            (1.0f / (INA199_GAIN_V_V * CURRENT_SHUNT_RESISTANCE_OHM))
#endif

#ifndef P0_CURRENT_SIGN_INVERT
#define P0_CURRENT_SIGN_INVERT           0U
#endif

#ifndef P0_ZERO_TRACK_DELAY_MS
#define P0_ZERO_TRACK_DELAY_MS           10U
#endif

#ifndef P0_ZERO_TRACK_ALPHA_STOP
#define P0_ZERO_TRACK_ALPHA_STOP         0.02f
#endif

typedef enum
{
    P0_STATE_WAIT_BT = 0,
    P0_STATE_READY,
    P0_STATE_PREPARE,
    P0_STATE_RAMP,
    P0_STATE_STABILIZE,
    P0_STATE_MEASURE,
    P0_STATE_REST,
    P0_STATE_DONE,
    P0_STATE_ABORTED,
    P0_STATE_FAULT
} p0_state_t;

typedef struct
{
    uint16_t adc_i_raw;
    uint16_t adc_vbat_raw;
    float v_i_sense;
    float v_vbat_adc;
    float delta_i_V;
    float active_zero_offset_V;
    float current_A;
    float vbat_V;
    float power_W;
} P0_AdcProcessed_t;

void P0_RpmMap_Init(void);
void P0_RpmMap_Task(void);
const char *P0_RpmMap_GetStateName(p0_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* APP_P0_RPM_MAP_H */

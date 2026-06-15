#ifndef APP_E3_TEST_H
#define APP_E3_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#ifndef E3_DSHOT_SEND_INTERVAL_MS
#define E3_DSHOT_SEND_INTERVAL_MS          1U
#endif

#ifndef E3_DSHOT_MIN_THROTTLE
#define E3_DSHOT_MIN_THROTTLE              48U
#endif

#ifndef E3_DSHOT_MAX_THROTTLE
#define E3_DSHOT_MAX_THROTTLE              2047U
#endif

#ifndef E3_DSHOT_TELEMETRY_BIT
#define E3_DSHOT_TELEMETRY_BIT             0U
#endif

#ifndef E3_PREPARE_MS
#define E3_PREPARE_MS                      10000U
#endif

#ifndef E3_RAMP_START_DSHOT
#define E3_RAMP_START_DSHOT                500U
#endif

#ifndef E3_RAMP_START_HOLD_MS
#define E3_RAMP_START_HOLD_MS              250U
#endif

#ifndef E3_RAMP_MS
#define E3_RAMP_MS                         5000U
#endif

#ifndef E3_STABILIZE_MS
#define E3_STABILIZE_MS                    10000U
#endif

#ifndef E3_ESC_ARMING_MS
#define E3_ESC_ARMING_MS                   6000U
#endif

#ifndef E3_P0_TARGET_R1_RPM
#define E3_P0_TARGET_R1_RPM                3000U
#endif

#ifndef E3_P0_TARGET_R2_RPM
#define E3_P0_TARGET_R2_RPM                6500U
#endif

#ifndef E3_P0_TARGET_R3_RPM
#define E3_P0_TARGET_R3_RPM                10000U
#endif

#ifndef E3_P0_SI_R1_DSHOT
#define E3_P0_SI_R1_DSHOT                  551U
#endif

#ifndef E3_P0_SI_R2_DSHOT
#define E3_P0_SI_R2_DSHOT                  786U
#endif

#ifndef E3_P0_SI_R3_DSHOT
#define E3_P0_SI_R3_DSHOT                  1124U
#endif

#ifndef E3_P0_GAN_R1_DSHOT
#define E3_P0_GAN_R1_DSHOT                 540U
#endif

#ifndef E3_P0_GAN_R2_DSHOT
#define E3_P0_GAN_R2_DSHOT                 762U
#endif

#ifndef E3_P0_GAN_R3_DSHOT
#define E3_P0_GAN_R3_DSHOT                 1022U
#endif

#ifndef E3_HEAT_SOAK_MS
#define E3_HEAT_SOAK_MS                    600000U
#endif

#ifndef E3_STOP_MS
#define E3_STOP_MS                         10000U
#endif

#ifndef E3_REMINDER_WINDOW_MS
#define E3_REMINDER_WINDOW_MS              5000U
#endif

#ifndef E3_ZERO_OFFSET_SAMPLE_INTERVAL_MS
#define E3_ZERO_OFFSET_SAMPLE_INTERVAL_MS  10U
#endif

#ifndef E3_CURRENT_SIGN_INVERT
#define E3_CURRENT_SIGN_INVERT             0U
#endif

#ifndef E3_ZERO_TRACK_DELAY_MS
#define E3_ZERO_TRACK_DELAY_MS             3000U
#endif

#ifndef E3_ZERO_TRACK_ALPHA_STOP
#define E3_ZERO_TRACK_ALPHA_STOP           0.01f
#endif

#ifndef E3_ZERO_TRACK_ALPHA_DONE
#define E3_ZERO_TRACK_ALPHA_DONE           0.02f
#endif

#ifndef E3_BUTTON_DEBOUNCE_MS
#define E3_BUTTON_DEBOUNCE_MS              30U
#endif

#ifndef E3_BUTTON_LONG_PRESS_MS
#define E3_BUTTON_LONG_PRESS_MS            800U
#endif

#ifndef E3_OLED_UPDATE_INTERVAL_MS
#define E3_OLED_UPDATE_INTERVAL_MS         200U
#endif

#ifndef E3_OLED_I2C_TIMEOUT_MS
#define E3_OLED_I2C_TIMEOUT_MS             25U
#endif

#ifndef E3_OLED_POWERUP_DELAY_MS
#define E3_OLED_POWERUP_DELAY_MS           120U
#endif

#ifndef E3_OLED_RETRY_INTERVAL_MS
#define E3_OLED_RETRY_INTERVAL_MS          500U
#endif

#ifndef E3_TRIGGER_PULSE_MS
#define E3_TRIGGER_PULSE_MS                50U
#endif

#ifndef E3_OLED_I2C_ADDR
#define E3_OLED_I2C_ADDR                   (0x3CU << 1)
#endif

#ifndef E3_OLED_COLUMN_OFFSET
#define E3_OLED_COLUMN_OFFSET              2U
#endif

#ifndef ADC_REF_VOLTAGE
#define ADC_REF_VOLTAGE                    3.3f
#endif

#ifndef ADC_MAX_COUNTS
#define ADC_MAX_COUNTS                     4095.0f
#endif

#ifndef VBAT_DIVIDER_RATIO
#define VBAT_DIVIDER_RATIO                 11.060484f
#endif

#ifndef CURRENT_OFFSET_V
#define CURRENT_OFFSET_V                   0.0f
#endif

#ifndef CURRENT_SCALE_A_PER_V
#define CURRENT_SCALE_A_PER_V              80.0f
#endif

typedef enum
{
    STATE_ARMING = 0,
    STATE_READY = 1,
    STATE_PREPARE = 2,
    STATE_RAMP = 3,
    STATE_STABILIZE = 4,
    STATE_HEAT_SOAK = 5,
    STATE_STOP = 6,
    STATE_DONE = 7,
    STATE_ABORTED = 8
} E3_TestState_t;

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
} E3_AdcProcessed_t;

typedef E3_TestState_t H1_TestState_t;
typedef E3_AdcProcessed_t H1_AdcProcessed_t;

#define H1_DSHOT_SEND_INTERVAL_MS         E3_DSHOT_SEND_INTERVAL_MS
#define H1_DSHOT_MIN_THROTTLE             E3_DSHOT_MIN_THROTTLE
#define H1_DSHOT_MAX_THROTTLE             E3_DSHOT_MAX_THROTTLE
#define H1_DSHOT_TELEMETRY_BIT            E3_DSHOT_TELEMETRY_BIT
#define H1_ZERO_OFFSET_SAMPLE_INTERVAL_MS E3_ZERO_OFFSET_SAMPLE_INTERVAL_MS
#define H1_CURRENT_SIGN_INVERT            E3_CURRENT_SIGN_INVERT
#define H1_ZERO_TRACK_DELAY_MS            E3_ZERO_TRACK_DELAY_MS
#define H1_ZERO_TRACK_ALPHA_STOP          E3_ZERO_TRACK_ALPHA_STOP
#define H1_ZERO_TRACK_ALPHA_DONE          E3_ZERO_TRACK_ALPHA_DONE
#define H1_BUTTON_DEBOUNCE_MS             E3_BUTTON_DEBOUNCE_MS
#define H1_OLED_UPDATE_INTERVAL_MS        E3_OLED_UPDATE_INTERVAL_MS
#define H1_OLED_I2C_TIMEOUT_MS            E3_OLED_I2C_TIMEOUT_MS
#define H1_OLED_POWERUP_DELAY_MS          E3_OLED_POWERUP_DELAY_MS
#define H1_OLED_RETRY_INTERVAL_MS         E3_OLED_RETRY_INTERVAL_MS
#define H1_OLED_I2C_ADDR                  E3_OLED_I2C_ADDR
#define H1_OLED_COLUMN_OFFSET             E3_OLED_COLUMN_OFFSET

void E3_Test_Init(void);
void E3_Test_Task(void);
const char *E3_Test_GetStateName(E3_TestState_t state);
void set_throttle_command(uint16_t cmd);
void process_adc_average(E3_AdcProcessed_t *out);
void update_zero_offset(const E3_AdcProcessed_t *adc_data, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_E3_TEST_H */

#ifndef APP_E2_WAVEFORM_H
#define APP_E2_WAVEFORM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Experiment command interface kept in "us-like" semantics so it is still
 * easy to tune from the test script side even though the transport is DShot.
 */
#ifndef E2_PWM_MIN_US
#define E2_PWM_MIN_US                  1000U
#endif

#ifndef E2_PWM_MAX_US
#define E2_PWM_MAX_US                  2000U
#endif

/* DShot transport tuning. */
#ifndef E2_DSHOT_SEND_INTERVAL_MS
#define E2_DSHOT_SEND_INTERVAL_MS      1U
#endif

#ifndef E2_DSHOT_MIN_THROTTLE
#define E2_DSHOT_MIN_THROTTLE          48U
#endif

#ifndef E2_DSHOT_MAX_THROTTLE
#define E2_DSHOT_MAX_THROTTLE          2047U
#endif

#ifndef E2_DSHOT_TELEMETRY_BIT
#define E2_DSHOT_TELEMETRY_BIT         0U
#endif

/* Experiment timing. */
#ifndef E2_BT_PREPARE_MS
#define E2_BT_PREPARE_MS               10000U
#endif

#ifndef E2_BT_CONNECT_CONFIRM_MS
#define E2_BT_CONNECT_CONFIRM_MS       500U
#endif

/* P0 same-RPM profile table selected from final tachometer mapping.
 * Button selection order is Si R1/R2/R3, then GaN R1/R2/R3.
 */
#ifndef E2_P0_TARGET_R1_RPM
#define E2_P0_TARGET_R1_RPM            3000U
#endif

#ifndef E2_P0_TARGET_R2_RPM
#define E2_P0_TARGET_R2_RPM            6500U
#endif

#ifndef E2_P0_TARGET_R3_RPM
#define E2_P0_TARGET_R3_RPM            10000U
#endif

#ifndef E2_P0_SI_R1_DSHOT
#define E2_P0_SI_R1_DSHOT              551U
#endif

#ifndef E2_P0_SI_R2_DSHOT
#define E2_P0_SI_R2_DSHOT              786U
#endif

#ifndef E2_P0_SI_R3_DSHOT
#define E2_P0_SI_R3_DSHOT              1124U
#endif

#ifndef E2_P0_GAN_R1_DSHOT
#define E2_P0_GAN_R1_DSHOT             540U
#endif

#ifndef E2_P0_GAN_R2_DSHOT
#define E2_P0_GAN_R2_DSHOT             762U
#endif

#ifndef E2_P0_GAN_R3_DSHOT
#define E2_P0_GAN_R3_DSHOT             1022U
#endif

#ifndef E2_RUN_THROTTLE_DSHOT
#define E2_RUN_THROTTLE_DSHOT          E2_P0_SI_R1_DSHOT
#endif

#ifndef E2_RUN_THROTTLE_MIN_DSHOT
#define E2_RUN_THROTTLE_MIN_DSHOT      48U
#endif

#ifndef E2_RUN_THROTTLE_MAX_DSHOT
#define E2_RUN_THROTTLE_MAX_DSHOT      2047U
#endif

#ifndef E2_RAMP_START_DSHOT
#define E2_RAMP_START_DSHOT            500U
#endif

#ifndef E2_RAMP_MS
#define E2_RAMP_MS                     5000U
#endif

#ifndef E2_RAMP_START_HOLD_MS
#define E2_RAMP_START_HOLD_MS          250U
#endif

#ifndef E2_STABILIZE_MS
#define E2_STABILIZE_MS                10000U
#endif

#ifndef E2_RUN_MS
#define E2_RUN_MS                      300000U
#endif

#ifndef E2_DONE_REARM_DELAY_MS
#define E2_DONE_REARM_DELAY_MS         5000U
#endif

#ifndef E2_SESSION_MAX_MS
#define E2_SESSION_MAX_MS \
    (E2_BT_PREPARE_MS + E2_RAMP_MS + E2_STABILIZE_MS + E2_RUN_MS + \
     E2_DONE_REARM_DELAY_MS + 5000U)
#endif

#ifndef E2_UART_BOOT_QUIET_MS
#define E2_UART_BOOT_QUIET_MS          2000U
#endif

#ifndef E2_UART_TX_BUFFER_SIZE
#define E2_UART_TX_BUFFER_SIZE         1024U
#endif

#ifndef E2_BUTTON_DEBOUNCE_MS
#define E2_BUTTON_DEBOUNCE_MS          30U
#endif

#ifndef E2_OLED_UPDATE_INTERVAL_MS
#define E2_OLED_UPDATE_INTERVAL_MS     200U
#endif

#ifndef E2_OLED_I2C_TIMEOUT_MS
#define E2_OLED_I2C_TIMEOUT_MS         25U
#endif

#ifndef E2_OLED_POWERUP_DELAY_MS
#define E2_OLED_POWERUP_DELAY_MS       120U
#endif

#ifndef E2_OLED_RETRY_INTERVAL_MS
#define E2_OLED_RETRY_INTERVAL_MS      500U
#endif

#ifndef E2_OLED_I2C_ADDR
#define E2_OLED_I2C_ADDR               (0x3CU << 1)
#endif

#ifndef E2_OLED_COLUMN_OFFSET
#define E2_OLED_COLUMN_OFFSET          2U
#endif

typedef enum
{
    STATE_WAIT_BT = 0,
    STATE_PREPARE = 1,
    STATE_RUN     = 2,
    STATE_STABILIZE = 3,
    STATE_DONE    = 4,
    STATE_IDLE    = 5,
    STATE_RAMP    = 6
} E2_TestState_t;

void E2_Test_Init(void);
void E2_Test_Task(void);
const char *E2_Test_GetStateName(E2_TestState_t state);
void set_throttle_us(uint16_t us);
void set_throttle_command(uint16_t cmd);

#ifdef __cplusplus
}
#endif

#endif /* APP_E2_WAVEFORM_H */


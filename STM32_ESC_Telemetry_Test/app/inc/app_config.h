#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define APP_RC_CHANNEL_COUNT                16U

#define APP_RC_THROTTLE_CH                  3U
#define APP_RC_ARM_CH                       5U

#define APP_RC_MIN_ENDPOINT_US              988U
#define APP_RC_MAX_ENDPOINT_US              2012U

#define APP_RC_MIN_COMMAND_US               APP_RC_MIN_ENDPOINT_US
#define APP_RC_LOW_THROTTLE_US              1050U
#define APP_RC_ARM_OFF_US                   1300U
#define APP_RC_ARM_ON_US                    1700U
#define APP_RC_MAX_COMMAND_US               APP_RC_MAX_ENDPOINT_US

#define APP_RX_FAILSAFE_TIMEOUT_MS          150U
#define APP_RX_STABLE_MS                    300U
#define APP_ARM_REQUIRE_SWITCH_CYCLE        0U

#define APP_DSHOT_SEND_INTERVAL_MS          1U
#define APP_DSHOT_MIN_THROTTLE              48U
#define APP_DSHOT_MAX_THROTTLE              2047U
#define APP_DSHOT_MAX_DEMO                  2047U
#define APP_DSHOT_TELEMETRY_BIT             0U

#define APP_MOTOR_SPIN_ARM_DSHOT            160U
#define APP_MOTOR_SPIN_MIN_DSHOT            220U
#define APP_MOTOR_IDLE_HOLD_MS              300U
#define APP_MOTOR_STOP_AT_LOW_THROTTLE      1U

#define APP_MOTOR_RAMP_UP_DSHOT_PER_MS      6U
#define APP_MOTOR_RAMP_DOWN_DSHOT_PER_MS    18U

#define APP_PROTECT_SOFT_CURRENT_A          10.0f
#define APP_PROTECT_HARD_CURRENT_A          16.0f
#define APP_PROTECT_REGEN_CURRENT_A         4.0f
#define APP_PROTECT_RELEASE_CURRENT_A       2.0f

#define APP_PROTECT_OVERVOLTAGE_V           17.40f
#define APP_PROTECT_RELEASE_VBAT_V          16.90f

#define APP_PROTECT_LATCH_MS                800U

#define APP_HC05_STATUS_INTERVAL_MS         500U
#define APP_HC05_UART_TIMEOUT_MS            200U

#define APP_KEY_DEBOUNCE_MS                 30U

#define APP_OLED_UPDATE_INTERVAL_MS         200U
#define APP_OLED_POWERUP_DELAY_MS           120U
#define APP_OLED_RETRY_INTERVAL_MS          500U
#define APP_OLED_I2C_ADDR                   (0x3CU << 1)
#define APP_OLED_COLUMN_OFFSET              2U

#define APP_ADC_CHANNEL_COUNT               2U
#define APP_ADC_DMA_SAMPLES_PER_CHANNEL     64U
#define APP_ADC_DMA_BUFFER_LENGTH           (APP_ADC_CHANNEL_COUNT * APP_ADC_DMA_SAMPLES_PER_CHANNEL)

#define APP_ADC_REF_VOLTAGE                 3.3f
#define APP_ADC_MAX_COUNTS                  4095.0f
#define APP_VBAT_DIVIDER_RATIO              11.060484f
#define APP_CURRENT_OFFSET_V                0.0f
#define APP_CURRENT_SCALE_A_PER_V           80.0f
#define APP_CURRENT_SIGN_INVERT             0U
#define APP_ZERO_OFFSET_SAMPLE_INTERVAL_MS  10U
#define APP_BASELINE_SAMPLE_COUNT           600U
#define APP_CURRENT_FILTER_INTERVAL_MS      10U
#define APP_CURRENT_FILTER_WINDOW_MS        250U
#define APP_CURRENT_FILTER_WINDOW_SAMPLES   (APP_CURRENT_FILTER_WINDOW_MS / APP_CURRENT_FILTER_INTERVAL_MS)

#define APP_HC05_CONNECTED_LEVEL            1U

#ifdef __cplusplus
}
#endif

#endif /* APP_CONFIG_H */

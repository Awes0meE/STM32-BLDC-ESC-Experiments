#ifndef APP_ARM_H
#define APP_ARM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
    APP_STATE_BOOT = 0,
    APP_STATE_WAIT_RX,
    APP_STATE_READY,
    APP_STATE_ARMED,
    APP_STATE_FAILSAFE,
    APP_STATE_ESTOP
} AppState_t;

typedef enum
{
    APP_ARM_DROP_NONE = 0,
    APP_ARM_DROP_RX_LOSS,
    APP_ARM_DROP_ARM_SWITCH_OFF,
    APP_ARM_DROP_ESTOP
} AppArmDropReason_t;

typedef struct
{
    uint8_t rx_active;
    uint16_t throttle_us;
    uint16_t arm_channel_us;
    uint8_t toggle_estop_request;
    uint8_t force_estop_request;
} AppArmInputs_t;

typedef struct
{
    AppState_t state;
    uint8_t armed;
    uint8_t estop_latched;
    uint8_t failsafe_active;
    uint8_t throttle_low;
    uint8_t arm_switch_on;
    uint8_t arm_switch_seen_off;
    uint8_t rx_link_established;
    AppArmDropReason_t drop_reason;
} AppArmStatus_t;

void App_Arm_Init(void);
void App_Arm_Update(const AppArmInputs_t *inputs, uint32_t now_ms);
void App_Arm_GetStatus(AppArmStatus_t *out_status);
const char *App_Arm_GetStateName(AppState_t state);
const char *App_Arm_GetDropReasonName(AppArmDropReason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* APP_ARM_H */

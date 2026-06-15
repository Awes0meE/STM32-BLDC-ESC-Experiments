#ifndef APP_MOTOR_H
#define APP_MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "bsp_adc_mon.h"

typedef enum
{
    APP_MOTOR_PROTECT_NONE = 0,
    APP_MOTOR_PROTECT_OVERCURRENT,
    APP_MOTOR_PROTECT_OVERVOLTAGE,
    APP_MOTOR_PROTECT_REGEN
} AppMotorProtectReason_t;

typedef struct
{
    uint8_t protect_active;
    uint8_t require_rearm;
    AppMotorProtectReason_t protect_reason;
    uint16_t requested_command;
    uint16_t output_command;
    uint32_t protect_trip_count;
} AppMotorStatus_t;

void App_Motor_Init(void);
uint16_t App_Motor_MapThrottleUsToDshot(uint16_t throttle_us);
uint16_t App_Motor_GetOutputCommand(uint8_t armed,
                                    uint16_t throttle_us,
                                    const BspAdcMonData_t *adc_data,
                                    uint32_t now_ms);
void App_Motor_GetStatus(AppMotorStatus_t *out_status);
const char *App_Motor_GetProtectReasonName(AppMotorProtectReason_t reason);

#ifdef __cplusplus
}
#endif

#endif /* APP_MOTOR_H */

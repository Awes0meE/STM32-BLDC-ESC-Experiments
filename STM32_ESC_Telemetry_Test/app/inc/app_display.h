#ifndef APP_DISPLAY_H
#define APP_DISPLAY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "app_arm.h"
#include "app_motor.h"

typedef struct
{
    AppState_t state;
    uint8_t rx_active;
    uint8_t armed;
    uint8_t protect_active;
    AppMotorProtectReason_t protect_reason;
    uint16_t throttle_us;
    uint16_t dshot_command;
    float vbat_v;
    float current_a;
} AppDisplayModel_t;

void App_Display_Init(void);
void App_Display_Task(const AppDisplayModel_t *model, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_DISPLAY_H */

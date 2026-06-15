#include "app_motor.h"

#include <string.h>

#include "app_config.h"

typedef struct
{
    AppMotorStatus_t status;
    uint32_t arm_entry_ms;
    uint32_t last_update_ms;
    uint32_t protect_release_ms;
    uint8_t prev_armed;
} AppMotorContext_t;

static AppMotorContext_t g_motor;

static uint16_t App_Motor_ApplySlewLimit(uint16_t requested, uint32_t now_ms);
static void App_Motor_LatchProtect(AppMotorProtectReason_t reason, uint32_t now_ms);
static uint8_t App_Motor_CanReleaseProtect(uint16_t throttle_us,
                                           const BspAdcMonData_t *adc_data,
                                           uint32_t now_ms);
static uint16_t App_Motor_GetArmedRequestedCommand(uint16_t throttle_us, uint32_t now_ms);

void App_Motor_Init(void)
{
    memset(&g_motor, 0, sizeof(g_motor));
}

uint16_t App_Motor_MapThrottleUsToDshot(uint16_t throttle_us)
{
    uint32_t scaled;

    if (throttle_us <= APP_RC_LOW_THROTTLE_US)
    {
        return 0U;
    }

    if (throttle_us >= APP_RC_MAX_COMMAND_US)
    {
        return APP_DSHOT_MAX_DEMO;
    }

    scaled = (uint32_t)(throttle_us - APP_RC_LOW_THROTTLE_US) *
        (uint32_t)(APP_DSHOT_MAX_DEMO - APP_MOTOR_SPIN_MIN_DSHOT);
    scaled /= (uint32_t)(APP_RC_MAX_COMMAND_US - APP_RC_LOW_THROTTLE_US);

    return (uint16_t)(APP_MOTOR_SPIN_MIN_DSHOT + scaled);
}

uint16_t App_Motor_GetOutputCommand(uint8_t armed,
                                    uint16_t throttle_us,
                                    const BspAdcMonData_t *adc_data,
                                    uint32_t now_ms)
{
    uint16_t requested_command = 0U;
    uint16_t output_command = 0U;
    uint8_t armed_rising_edge;
    uint8_t armed_falling_edge;

    armed_rising_edge = (g_motor.prev_armed == 0U) && (armed != 0U);
    armed_falling_edge = (g_motor.prev_armed != 0U) && (armed == 0U);

    if (armed_rising_edge != 0U)
    {
        g_motor.arm_entry_ms = now_ms;
        g_motor.last_update_ms = now_ms;
    }

    if (armed != 0U)
    {
        requested_command = App_Motor_GetArmedRequestedCommand(throttle_us, now_ms);
    }

    g_motor.status.requested_command = requested_command;

    if (armed_falling_edge != 0U)
    {
        g_motor.status.output_command = 0U;
        if ((g_motor.status.protect_active != 0U) &&
            (g_motor.status.require_rearm != 0U) &&
            (App_Motor_CanReleaseProtect(throttle_us, adc_data, now_ms) != 0U))
        {
            g_motor.status.protect_active = 0U;
            g_motor.status.protect_reason = APP_MOTOR_PROTECT_NONE;
            g_motor.status.require_rearm = 0U;
        }
    }

    if ((armed != 0U) && (adc_data != NULL))
    {
        if (adc_data->vbat_v >= APP_PROTECT_OVERVOLTAGE_V)
        {
            App_Motor_LatchProtect(APP_MOTOR_PROTECT_OVERVOLTAGE, now_ms);
        }
        else if (adc_data->current_a <= (-APP_PROTECT_REGEN_CURRENT_A))
        {
            App_Motor_LatchProtect(APP_MOTOR_PROTECT_REGEN, now_ms);
        }
        else if (adc_data->current_abs_a >= APP_PROTECT_HARD_CURRENT_A)
        {
            App_Motor_LatchProtect(APP_MOTOR_PROTECT_OVERCURRENT, now_ms);
        }
    }

    if (g_motor.status.protect_active != 0U)
    {
        g_motor.prev_armed = armed;
        g_motor.status.output_command = 0U;
        g_motor.last_update_ms = now_ms;
        return 0U;
    }

    if (armed == 0U)
    {
        g_motor.prev_armed = 0U;
        g_motor.status.output_command = 0U;
        g_motor.last_update_ms = now_ms;
        return 0U;
    }

    if ((adc_data != NULL) &&
        (adc_data->current_abs_a >= APP_PROTECT_SOFT_CURRENT_A) &&
        (requested_command > g_motor.status.output_command))
    {
        requested_command = g_motor.status.output_command;
    }

    output_command = App_Motor_ApplySlewLimit(requested_command, now_ms);

    if (output_command > APP_DSHOT_MAX_THROTTLE)
    {
        output_command = APP_DSHOT_MAX_THROTTLE;
    }

    if ((output_command != 0U) && (output_command < APP_DSHOT_MIN_THROTTLE))
    {
        output_command = APP_DSHOT_MIN_THROTTLE;
    }

    g_motor.status.output_command = output_command;
    g_motor.prev_armed = armed;
    return output_command;
}

void App_Motor_GetStatus(AppMotorStatus_t *out_status)
{
    if (out_status == NULL)
    {
        return;
    }

    *out_status = g_motor.status;
}

const char *App_Motor_GetProtectReasonName(AppMotorProtectReason_t reason)
{
    switch (reason)
    {
    case APP_MOTOR_PROTECT_OVERCURRENT:
        return "OC";
    case APP_MOTOR_PROTECT_OVERVOLTAGE:
        return "OVP";
    case APP_MOTOR_PROTECT_REGEN:
        return "REG";
    case APP_MOTOR_PROTECT_NONE:
    default:
        return "NONE";
    }
}

static uint16_t App_Motor_ApplySlewLimit(uint16_t requested, uint32_t now_ms)
{
    uint16_t current = g_motor.status.output_command;
    uint32_t elapsed_ms;
    uint32_t step;

    if (g_motor.last_update_ms == 0U)
    {
        g_motor.last_update_ms = now_ms;
        return requested;
    }

    elapsed_ms = now_ms - g_motor.last_update_ms;
    if (elapsed_ms == 0U)
    {
        elapsed_ms = 1U;
    }

    g_motor.last_update_ms = now_ms;

    if (requested > current)
    {
        step = elapsed_ms * APP_MOTOR_RAMP_UP_DSHOT_PER_MS;
        if ((uint32_t)requested > ((uint32_t)current + step))
        {
            return (uint16_t)(current + step);
        }
    }
    else if (requested < current)
    {
        step = elapsed_ms * APP_MOTOR_RAMP_DOWN_DSHOT_PER_MS;
        if ((uint32_t)(current - requested) > step)
        {
            return (uint16_t)(current - step);
        }
    }

    return requested;
}

static void App_Motor_LatchProtect(AppMotorProtectReason_t reason, uint32_t now_ms)
{
    if (reason == APP_MOTOR_PROTECT_NONE)
    {
        return;
    }

    g_motor.status.protect_active = 1U;
    g_motor.status.require_rearm = 1U;
    g_motor.status.protect_reason = reason;
    g_motor.status.output_command = 0U;
    g_motor.protect_release_ms = now_ms + APP_PROTECT_LATCH_MS;
    g_motor.status.protect_trip_count++;
}

static uint8_t App_Motor_CanReleaseProtect(uint16_t throttle_us,
                                           const BspAdcMonData_t *adc_data,
                                           uint32_t now_ms)
{
    if (now_ms < g_motor.protect_release_ms)
    {
        return 0U;
    }

    if (throttle_us > APP_RC_LOW_THROTTLE_US)
    {
        return 0U;
    }

    if (adc_data == NULL)
    {
        return 1U;
    }

    if (adc_data->current_abs_a > APP_PROTECT_RELEASE_CURRENT_A)
    {
        return 0U;
    }

    if (adc_data->vbat_v > APP_PROTECT_RELEASE_VBAT_V)
    {
        return 0U;
    }

    return 1U;
}

static uint16_t App_Motor_GetArmedRequestedCommand(uint16_t throttle_us, uint32_t now_ms)
{
#if (APP_MOTOR_STOP_AT_LOW_THROTTLE != 0U)
    (void)now_ms;

    if (throttle_us <= APP_RC_LOW_THROTTLE_US)
    {
        return 0U;
    }

    return App_Motor_MapThrottleUsToDshot(throttle_us);
#else
    if ((now_ms - g_motor.arm_entry_ms) < APP_MOTOR_IDLE_HOLD_MS)
    {
        return APP_MOTOR_SPIN_ARM_DSHOT;
    }

    if (throttle_us <= APP_RC_LOW_THROTTLE_US)
    {
        return APP_MOTOR_SPIN_ARM_DSHOT;
    }

    return App_Motor_MapThrottleUsToDshot(throttle_us);
#endif
}

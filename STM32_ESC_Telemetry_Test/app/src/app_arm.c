#include "app_arm.h"

#include <string.h>

#include "app_config.h"

typedef struct
{
    AppArmStatus_t status;
    uint32_t rx_good_since_ms;
    uint8_t ever_rx_link_established;
    uint8_t prev_arm_switch_on;
} AppArmContext_t;

static AppArmContext_t g_arm;

static uint8_t App_Arm_IsThrottleLow(uint16_t throttle_us);
static uint8_t App_Arm_UpdateArmSwitchState(uint16_t arm_channel_us);

void App_Arm_Init(void)
{
    memset(&g_arm, 0, sizeof(g_arm));
    g_arm.status.state = APP_STATE_BOOT;
    g_arm.status.drop_reason = APP_ARM_DROP_NONE;
}

void App_Arm_Update(const AppArmInputs_t *inputs, uint32_t now_ms)
{
    uint8_t allow_arm;
    uint8_t throttle_low;
    uint8_t arm_switch_on;

    if (inputs == NULL)
    {
        return;
    }

    throttle_low = App_Arm_IsThrottleLow(inputs->throttle_us);
    arm_switch_on = App_Arm_UpdateArmSwitchState(inputs->arm_channel_us);

    g_arm.status.throttle_low = throttle_low;
    g_arm.status.arm_switch_on = arm_switch_on;

    if (inputs->force_estop_request != 0U)
    {
        g_arm.status.estop_latched = 1U;
        g_arm.status.armed = 0U;
        g_arm.status.drop_reason = APP_ARM_DROP_ESTOP;
        if (arm_switch_on != 0U)
        {
            g_arm.status.arm_switch_seen_off = 0U;
        }
    }

    if (inputs->toggle_estop_request != 0U)
    {
        if (g_arm.status.estop_latched == 0U)
        {
            g_arm.status.estop_latched = 1U;
            g_arm.status.armed = 0U;
            g_arm.status.drop_reason = APP_ARM_DROP_ESTOP;
            if (arm_switch_on != 0U)
            {
                g_arm.status.arm_switch_seen_off = 0U;
            }
        }
        else if ((inputs->rx_active != 0U) &&
                 (throttle_low != 0U) &&
                 (arm_switch_on == 0U))
        {
            g_arm.status.estop_latched = 0U;
        }
    }

    if (inputs->rx_active != 0U)
    {
        if (g_arm.rx_good_since_ms == 0U)
        {
            g_arm.rx_good_since_ms = now_ms;
        }
        else if ((now_ms - g_arm.rx_good_since_ms) >= APP_RX_STABLE_MS)
        {
            g_arm.status.rx_link_established = 1U;
            g_arm.ever_rx_link_established = 1U;
        }
    }
    else
    {
        g_arm.rx_good_since_ms = 0U;
        g_arm.status.rx_link_established = 0U;
        if (g_arm.status.armed != 0U)
        {
            g_arm.status.drop_reason = APP_ARM_DROP_RX_LOSS;
        }
        g_arm.status.armed = 0U;
        if (arm_switch_on != 0U)
        {
            g_arm.status.arm_switch_seen_off = 0U;
        }
    }

    if (arm_switch_on == 0U)
    {
        g_arm.status.arm_switch_seen_off = 1U;
    }

    g_arm.prev_arm_switch_on = arm_switch_on;

    if ((g_arm.status.armed != 0U) &&
        ((inputs->rx_active == 0U) ||
         (arm_switch_on == 0U) ||
         (g_arm.status.estop_latched != 0U)))
    {
        if (inputs->rx_active == 0U)
        {
            g_arm.status.drop_reason = APP_ARM_DROP_RX_LOSS;
        }
        else if (arm_switch_on == 0U)
        {
            g_arm.status.drop_reason = APP_ARM_DROP_ARM_SWITCH_OFF;
        }
        else
        {
            g_arm.status.drop_reason = APP_ARM_DROP_ESTOP;
        }
        g_arm.status.armed = 0U;
        if (arm_switch_on != 0U)
        {
            g_arm.status.arm_switch_seen_off = 0U;
        }
    }

    allow_arm = 0U;
    if ((inputs->rx_active != 0U) &&
        (g_arm.status.rx_link_established != 0U) &&
        (g_arm.status.estop_latched == 0U) &&
        (throttle_low != 0U) &&
        (arm_switch_on != 0U))
    {
#if (APP_ARM_REQUIRE_SWITCH_CYCLE != 0U)
        if (g_arm.status.arm_switch_seen_off != 0U)
        {
            allow_arm = 1U;
        }
#else
        allow_arm = 1U;
#endif
    }

    if (allow_arm != 0U)
    {
        g_arm.status.armed = 1U;
        g_arm.status.drop_reason = APP_ARM_DROP_NONE;
    }

    if (g_arm.status.estop_latched != 0U)
    {
        g_arm.status.state = APP_STATE_ESTOP;
        g_arm.status.failsafe_active = 0U;
    }
    else if (inputs->rx_active == 0U)
    {
        g_arm.status.state = (g_arm.ever_rx_link_established != 0U) ? APP_STATE_FAILSAFE : APP_STATE_WAIT_RX;
        g_arm.status.failsafe_active = (g_arm.ever_rx_link_established != 0U) ? 1U : 0U;
    }
    else if (g_arm.status.armed != 0U)
    {
        g_arm.status.state = APP_STATE_ARMED;
        g_arm.status.failsafe_active = 0U;
    }
    else if (g_arm.status.rx_link_established == 0U)
    {
        g_arm.status.state = APP_STATE_WAIT_RX;
        g_arm.status.failsafe_active = 0U;
    }
    else
    {
        g_arm.status.state = APP_STATE_READY;
        g_arm.status.failsafe_active = 0U;
    }
}

void App_Arm_GetStatus(AppArmStatus_t *out_status)
{
    if (out_status == NULL)
    {
        return;
    }

    *out_status = g_arm.status;
}

const char *App_Arm_GetStateName(AppState_t state)
{
    switch (state)
    {
    case APP_STATE_BOOT:
        return "BOOT";
    case APP_STATE_WAIT_RX:
        return "WAIT_RX";
    case APP_STATE_READY:
        return "READY";
    case APP_STATE_ARMED:
        return "ARMED";
    case APP_STATE_FAILSAFE:
        return "FAILSAFE";
    case APP_STATE_ESTOP:
        return "ESTOP";
    default:
        return "UNKNOWN";
    }
}

const char *App_Arm_GetDropReasonName(AppArmDropReason_t reason)
{
    switch (reason)
    {
    case APP_ARM_DROP_RX_LOSS:
        return "RX_LOSS";
    case APP_ARM_DROP_ARM_SWITCH_OFF:
        return "ARM_SW";
    case APP_ARM_DROP_ESTOP:
        return "ESTOP";
    case APP_ARM_DROP_NONE:
    default:
        return "NONE";
    }
}

static uint8_t App_Arm_IsThrottleLow(uint16_t throttle_us)
{
    return (throttle_us <= APP_RC_LOW_THROTTLE_US) ? 1U : 0U;
}

static uint8_t App_Arm_UpdateArmSwitchState(uint16_t arm_channel_us)
{
    if (arm_channel_us >= APP_RC_ARM_ON_US)
    {
        return 1U;
    }

    if (arm_channel_us <= APP_RC_ARM_OFF_US)
    {
        return 0U;
    }

    return g_arm.status.arm_switch_on;
}

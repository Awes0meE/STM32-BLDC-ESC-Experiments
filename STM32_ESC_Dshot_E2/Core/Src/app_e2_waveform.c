#include "app_e2_waveform.h"

#include <stdio.h>
#include <string.h>

#include "gpio.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"

#define E2_DSHOT_FRAME_BITS               16U
#define E2_DSHOT_RESET_SLOTS              8U
#define E2_DSHOT_DMA_BUFFER_LENGTH        (E2_DSHOT_FRAME_BITS + E2_DSHOT_RESET_SLOTS)
#define E2_DSHOT_BIT_0_HIGH_TICKS         90U
#define E2_DSHOT_BIT_1_HIGH_TICKS         180U

#define E2_STATUS_LED_ON_LEVEL            GPIO_PIN_RESET
#define E2_STATUS_LED_OFF_LEVEL           GPIO_PIN_SET
#define E2_BT_CONNECTED_LEVEL             GPIO_PIN_SET

typedef struct
{
    const char *board_name;
    const char *oled_name;
    const char *rpm_point;
    uint16_t target_rpm;
    uint16_t dshot_cmd;
} E2_Profile_t;

static const E2_Profile_t g_e2_profiles[] =
{
    { "Si",  "SI",  "R1", E2_P0_TARGET_R1_RPM, E2_P0_SI_R1_DSHOT  },
    { "Si",  "SI",  "R2", E2_P0_TARGET_R2_RPM, E2_P0_SI_R2_DSHOT  },
    { "Si",  "SI",  "R3", E2_P0_TARGET_R3_RPM, E2_P0_SI_R3_DSHOT  },
    { "GaN", "GAN", "R1", E2_P0_TARGET_R1_RPM, E2_P0_GAN_R1_DSHOT },
    { "GaN", "GAN", "R2", E2_P0_TARGET_R2_RPM, E2_P0_GAN_R2_DSHOT },
    { "GaN", "GAN", "R3", E2_P0_TARGET_R3_RPM, E2_P0_GAN_R3_DSHOT }
};

#define E2_PROFILE_COUNT                  ((uint8_t)(sizeof(g_e2_profiles) / sizeof(g_e2_profiles[0])))

typedef struct
{
    E2_TestState_t state;
    uint32_t test_start_tick;
    uint32_t state_enter_tick;
    uint32_t last_dshot_send_tick;
    uint32_t last_oled_update_tick;
    uint32_t last_oled_init_attempt_tick;
    uint32_t button_change_tick;
    uint32_t bt_high_since_tick;
    uint32_t ramp_start_tick;
    uint16_t active_dshot_value;
    uint16_t run_cmd_dshot;
    uint16_t ramp_start_dshot;
    uint16_t ramp_target_dshot;
    uint8_t boot_banner_sent;
    uint8_t connected_banner_sent;
    uint8_t wait_start_banner_sent;
    volatile uint8_t dshot_dma_busy;
    uint8_t bt_state_last;
    uint8_t bt_confirmed;
    uint8_t bt_unsolicited_allowed;
    uint8_t start_command_received;
    uint8_t start_ack_pending;
    uint8_t start_armed_once;
    uint8_t button_raw_pressed;
    uint8_t button_stable_pressed;
    uint8_t oled_ready;
    uint8_t profile_index;
} E2_TestContext_t;

static E2_TestContext_t g_e2;
static uint16_t g_dshot_dma_buffer[E2_DSHOT_DMA_BUFFER_LENGTH];
static uint8_t g_oled_buffer[128U * 8U];
static uint8_t g_uart_rx_byte;
static char g_uart_cmd_buffer[16];
static uint8_t g_uart_cmd_index;
static uint8_t g_uart_tx_buffer[E2_UART_TX_BUFFER_SIZE];
static volatile uint16_t g_uart_tx_head;
static volatile uint16_t g_uart_tx_tail;
static volatile uint8_t g_uart_tx_busy;
static volatile uint8_t g_uart_rx_active;
static uint8_t g_uart_tx_byte;

static void E2_Test_EnterState(E2_TestState_t new_state);
static void E2_StartSession(uint32_t now_ms);
static uint8_t E2_IsBluetoothConnected(void);
static void E2_SendBootBannerOnce(void);
static void E2_SendConnectedBannerOnce(void);
static void E2_SendWaitStartBannerOnce(void);
static void E2_SendStartAck(void);
static void E2_SendStateChangeLine(E2_TestState_t state);
static uint8_t E2_UartWrite(const char *text);
static uint8_t E2_UartCanSend(void);
static void E2_UartTx_StartNext(void);
static void E2_UartTx_ResetQueue(void);
static void E2_StatusLed_Set(uint8_t on);
static void E2_StatusLed_Update(uint32_t now_ms);
static uint16_t E2_MapUsToDshot(uint16_t us);
static uint16_t E2_ClampDshotThrottle(uint16_t cmd);
static uint16_t E2_Dshot_BuildPacket(uint16_t throttle_value);
static void E2_Dshot_PrepareFrame(uint16_t throttle_value);
static void E2_Dshot_TriggerFrame(uint16_t throttle_value);
static void E2_Dshot_Service(uint32_t now_ms);
static void E2_UartRx_Start(void);
static void E2_ProcessReceivedByte(uint8_t byte);
static uint8_t E2_IsStartCommand(const char *text);
static uint8_t E2_IsStopCommand(const char *text);
static uint8_t E2_TryApplyProfileCommand(const char *text);
static void E2_ProcessStartCommand(void);
static void E2_ProcessStopCommand(void);
static uint16_t E2_GetRampStartDshot(void);
static void E2_UpdateRampThrottle(uint32_t now_ms);
static void E2_ForceMotorStopFrame(void);
static void E2_Button_Task(uint32_t now_ms);
static void E2_HandleThrottleButtonPress(void);
static const E2_Profile_t *E2_GetSelectedProfile(void);
static void E2_ApplyProfile(uint8_t profile_index);
static void E2_SendProfileLine(const char *event);
static void E2_ScopeMarker_Set(uint8_t high);
static HAL_StatusTypeDef E2_Oled_WriteCommand(uint8_t command);
static HAL_StatusTypeDef E2_Oled_WriteData(const uint8_t *data, uint16_t size);
static void E2_Oled_Init(void);
static void E2_Oled_Service(uint32_t now_ms);
static void E2_Oled_Clear(void);
static void E2_Oled_Flush(void);
static void E2_Oled_Update(uint32_t now_ms);
static void E2_Oled_DrawLine(uint8_t page, const char *text);
static void E2_Oled_DrawChar(uint8_t x, uint8_t y, char c);
static void E2_Oled_SetPixel(uint8_t x, uint8_t y, uint8_t on);
static void E2_Oled_GetGlyph5x7(char c, uint8_t glyph[5]);

void E2_Test_Init(void)
{
    memset(&g_e2, 0, sizeof(g_e2));

    g_e2.test_start_tick = HAL_GetTick();
    g_e2.state_enter_tick = g_e2.test_start_tick;
    g_e2.state = STATE_WAIT_BT;
    g_e2.active_dshot_value = 0U;
    E2_ApplyProfile(0U);
    g_e2.bt_state_last = E2_IsBluetoothConnected();
    g_e2.bt_unsolicited_allowed = (g_e2.bt_state_last == 0U) ? 1U : 0U;

    if (g_e2.run_cmd_dshot < E2_RUN_THROTTLE_MIN_DSHOT)
    {
        g_e2.run_cmd_dshot = E2_RUN_THROTTLE_MIN_DSHOT;
    }
    else if (g_e2.run_cmd_dshot > E2_RUN_THROTTLE_MAX_DSHOT)
    {
        g_e2.run_cmd_dshot = E2_RUN_THROTTLE_MAX_DSHOT;
    }

    set_throttle_command(0U);

    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);

    E2_UartRx_Start();
    HAL_Delay(E2_OLED_POWERUP_DELAY_MS);
    E2_Oled_Service(HAL_GetTick());
    E2_ScopeMarker_Set(0U);
    E2_Dshot_TriggerFrame(g_e2.active_dshot_value);
    E2_StatusLed_Update(HAL_GetTick());
}

void E2_Test_Task(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint32_t state_elapsed_ms;
    uint8_t bt_connected;

    if (g_e2.state == STATE_IDLE)
    {
        return;
    }

    E2_UartRx_Start();
    E2_UpdateRampThrottle(now_ms);
    E2_Dshot_Service(now_ms);
    E2_StatusLed_Update(now_ms);
    E2_Button_Task(now_ms);
    E2_Oled_Service(now_ms);
    E2_Oled_Update(now_ms);

    bt_connected = E2_IsBluetoothConnected();
    if ((bt_connected == 0U) &&
        ((g_e2.state == STATE_PREPARE) || (g_e2.state == STATE_RAMP) ||
         (g_e2.state == STATE_STABILIZE) || (g_e2.state == STATE_RUN)))
    {
        E2_ForceMotorStopFrame();
        g_e2.state = STATE_WAIT_BT;
        g_e2.state_enter_tick = now_ms;
        g_e2.test_start_tick = now_ms;
    }

    if (g_e2.state == STATE_WAIT_BT)
    {
        if (bt_connected != 0U)
        {
            if (g_e2.bt_state_last == 0U)
            {
                g_e2.bt_high_since_tick = now_ms;
            }

            if ((now_ms - g_e2.bt_high_since_tick) >= E2_BT_CONNECT_CONFIRM_MS)
            {
                g_e2.bt_confirmed = 1U;
                if (g_e2.bt_unsolicited_allowed != 0U)
                {
                    E2_SendBootBannerOnce();
                    E2_SendConnectedBannerOnce();
                    E2_SendWaitStartBannerOnce();
                }
            }

            if (g_e2.start_ack_pending != 0U)
            {
                E2_SendStartAck();
            }

            if ((g_e2.bt_confirmed != 0U) && (g_e2.start_command_received != 0U))
            {
                E2_StartSession(now_ms);
                return;
            }
        }
        else
        {
            g_e2.bt_high_since_tick = 0U;
            g_e2.bt_confirmed = 0U;
            g_e2.start_command_received = 0U;
            g_e2.start_ack_pending = 0U;
            g_e2.start_armed_once = 0U;
            g_e2.boot_banner_sent = 0U;
            g_e2.connected_banner_sent = 0U;
            g_e2.wait_start_banner_sent = 0U;
            g_e2.bt_unsolicited_allowed = 1U;
            E2_UartTx_ResetQueue();
        }
    }
    g_e2.bt_state_last = bt_connected;

    if ((bt_connected != 0U) && (g_e2.start_ack_pending != 0U))
    {
        E2_SendStartAck();
    }

    if ((g_e2.state != STATE_WAIT_BT) && ((now_ms - g_e2.test_start_tick) >= E2_SESSION_MAX_MS))
    {
        E2_Test_EnterState(STATE_IDLE);
        return;
    }

    state_elapsed_ms = now_ms - g_e2.state_enter_tick;

    switch (g_e2.state)
    {
    case STATE_WAIT_BT:
        break;

    case STATE_PREPARE:
        if (state_elapsed_ms >= E2_BT_PREPARE_MS)
        {
            E2_Test_EnterState(STATE_RAMP);
        }
        break;

    case STATE_RAMP:
        if (state_elapsed_ms >= E2_RAMP_MS)
        {
            E2_Test_EnterState(STATE_STABILIZE);
        }
        break;

    case STATE_STABILIZE:
        if (state_elapsed_ms >= E2_STABILIZE_MS)
        {
            E2_Test_EnterState(STATE_RUN);
        }
        break;

    case STATE_RUN:
        if (state_elapsed_ms >= E2_RUN_MS)
        {
            E2_Test_EnterState(STATE_DONE);
        }
        break;

    case STATE_DONE:
        if (state_elapsed_ms >= E2_DONE_REARM_DELAY_MS)
        {
            g_e2.start_command_received = 0U;
            g_e2.start_ack_pending = 0U;
            g_e2.start_armed_once = 0U;
            g_e2.test_start_tick = now_ms;
            E2_Test_EnterState(STATE_WAIT_BT);
            E2_SendProfileLine("rearmed");
        }
        break;

    case STATE_IDLE:
    default:
        break;
    }
}

const char *E2_Test_GetStateName(E2_TestState_t state)
{
    switch (state)
    {
    case STATE_WAIT_BT:
        return "WAIT_BT";
    case STATE_PREPARE:
        return "PREPARE";
    case STATE_RAMP:
        return "RAMP";
    case STATE_RUN:
        return "RUN";
    case STATE_STABILIZE:
        return "STABILIZE";
    case STATE_DONE:
        return "DONE";
    case STATE_IDLE:
        return "IDLE";
    default:
        return "UNKNOWN";
    }
}

void set_throttle_us(uint16_t us)
{
    uint16_t pulse_us = us;

    if (pulse_us < E2_PWM_MIN_US)
    {
        pulse_us = E2_PWM_MIN_US;
    }
    else if (pulse_us > E2_PWM_MAX_US)
    {
        pulse_us = E2_PWM_MAX_US;
    }

    g_e2.active_dshot_value = E2_MapUsToDshot(pulse_us);
}

void set_throttle_command(uint16_t cmd)
{
    g_e2.active_dshot_value = E2_ClampDshotThrottle(cmd);
}

static void E2_UpdateRampThrottle(uint32_t now_ms)
{
    uint32_t elapsed_ms;
    uint32_t hold_ms;
    uint32_t ramp_elapsed_ms;
    uint32_t ramp_duration_ms;
    uint32_t delta;
    uint32_t ramped_cmd;
    uint16_t ramp_start;
    uint16_t ramp_target;

    if (g_e2.state != STATE_RAMP)
    {
        return;
    }

    ramp_start = g_e2.ramp_start_dshot;
    ramp_target = g_e2.ramp_target_dshot;
    if (g_e2.ramp_start_tick == 0U)
    {
        g_e2.ramp_start_tick = g_e2.state_enter_tick;
    }

    elapsed_ms = now_ms - g_e2.ramp_start_tick;
    hold_ms = E2_RAMP_START_HOLD_MS;
    if (E2_RAMP_MS <= 1U)
    {
        hold_ms = 0U;
    }
    else if (hold_ms >= E2_RAMP_MS)
    {
        hold_ms = E2_RAMP_MS - 1U;
    }

    if (elapsed_ms >= E2_RAMP_MS)
    {
        set_throttle_command(ramp_target);
        return;
    }

    if ((hold_ms > 0U) && (elapsed_ms < hold_ms))
    {
        set_throttle_command(ramp_start);
        return;
    }

    ramp_elapsed_ms = elapsed_ms;
    ramp_duration_ms = E2_RAMP_MS;
    if (hold_ms > 0U)
    {
        ramp_elapsed_ms = elapsed_ms - hold_ms;
        ramp_duration_ms = E2_RAMP_MS - hold_ms;
    }

    if (ramp_target >= ramp_start)
    {
        delta = (uint32_t)ramp_target - (uint32_t)ramp_start;
        ramped_cmd = (uint32_t)ramp_start + ((delta * ramp_elapsed_ms) / ramp_duration_ms);
    }
    else
    {
        delta = (uint32_t)ramp_start - (uint32_t)ramp_target;
        ramped_cmd = (uint32_t)ramp_start - ((delta * ramp_elapsed_ms) / ramp_duration_ms);
    }

    set_throttle_command((uint16_t)ramped_cmd);
}

static uint16_t E2_GetRampStartDshot(void)
{
    uint16_t ramp_start;

    ramp_start = E2_ClampDshotThrottle(E2_RAMP_START_DSHOT);
    return ramp_start;
}

static void E2_Test_EnterState(E2_TestState_t new_state)
{
    g_e2.state = new_state;
    g_e2.state_enter_tick = HAL_GetTick();

    switch (new_state)
    {
    case STATE_WAIT_BT:
    case STATE_PREPARE:
    case STATE_DONE:
    case STATE_IDLE:
        g_e2.ramp_start_tick = 0U;
        g_e2.ramp_start_dshot = 0U;
        g_e2.ramp_target_dshot = 0U;
        set_throttle_command(0U);
        break;

    case STATE_RAMP:
        g_e2.ramp_start_tick = g_e2.state_enter_tick;
        g_e2.ramp_start_dshot = E2_GetRampStartDshot();
        g_e2.ramp_target_dshot = g_e2.run_cmd_dshot;
        set_throttle_command(g_e2.ramp_start_dshot);
        g_e2.last_dshot_send_tick = 0U;
        E2_Dshot_Service(g_e2.state_enter_tick);
        break;

    case STATE_STABILIZE:
        g_e2.ramp_start_tick = 0U;
        g_e2.ramp_start_dshot = 0U;
        g_e2.ramp_target_dshot = 0U;
        set_throttle_command(g_e2.run_cmd_dshot);
        break;

    case STATE_RUN:
        g_e2.ramp_start_tick = 0U;
        g_e2.ramp_start_dshot = 0U;
        g_e2.ramp_target_dshot = 0U;
        set_throttle_command(g_e2.run_cmd_dshot);
        break;

    default:
        g_e2.ramp_start_tick = 0U;
        g_e2.ramp_start_dshot = 0U;
        g_e2.ramp_target_dshot = 0U;
        set_throttle_command(0U);
        break;
    }

    if (new_state == STATE_IDLE)
    {
        E2_ForceMotorStopFrame();
    }

    E2_ScopeMarker_Set((new_state == STATE_RUN) ? 1U : 0U);
    E2_SendStateChangeLine(new_state);
    E2_StatusLed_Update(g_e2.state_enter_tick);
}

static void E2_StartSession(uint32_t now_ms)
{
    E2_UartTx_ResetQueue();

    g_e2.test_start_tick = now_ms;
    g_e2.start_command_received = 0U;
    g_e2.start_ack_pending = 1U;
    E2_ApplyProfile(g_e2.profile_index);
    E2_SendStartAck();
    E2_SendProfileLine("start_profile");
    g_e2.start_armed_once = 0U;

    E2_Test_EnterState(STATE_PREPARE);
}

static uint8_t E2_IsBluetoothConnected(void)
{
    return (HAL_GPIO_ReadPin(BT_STATE_GPIO_Port, BT_STATE_Pin) == E2_BT_CONNECTED_LEVEL) ? 1U : 0U;
}

static void E2_SendBootBannerOnce(void)
{
    static const char banner[] =
        "# E2 scope firmware ready. PB0 marks the RUN capture window.\r\n";

    if (g_e2.boot_banner_sent == 0U)
    {
        if (E2_UartWrite(banner) != 0U)
        {
            g_e2.boot_banner_sent = 1U;
        }
    }
}

static void E2_SendConnectedBannerOnce(void)
{
    const E2_Profile_t *profile = E2_GetSelectedProfile();
    char line[192];
    int len;

    if (g_e2.connected_banner_sent != 0U)
    {
        return;
    }

    len = snprintf(line,
                   sizeof(line),
                   "# Bluetooth connected. Selected P%u %s %s target %u rpm, DShot %u.\r\n",
                   (unsigned int)(g_e2.profile_index + 1U),
                   profile->board_name,
                   profile->rpm_point,
                   (unsigned int)profile->target_rpm,
                   (unsigned int)g_e2.run_cmd_dshot);
    if (len > 0)
    {
        if (E2_UartWrite(line) == 0U)
        {
            return;
        }
    }

    g_e2.connected_banner_sent = 1U;
}

static void E2_SendWaitStartBannerOnce(void)
{
    static const char line[] =
        "# Select P1-P6 if needed, then send START. STOP or ABORT stops the motor.\r\n";

    if (g_e2.wait_start_banner_sent == 0U)
    {
        if (E2_UartWrite(line) != 0U)
        {
            g_e2.wait_start_banner_sent = 1U;
        }
    }
}

static void E2_SendStartAck(void)
{
    const E2_Profile_t *profile = E2_GetSelectedProfile();
    char line[192];
    int len;

    len = snprintf(line,
                   sizeof(line),
                   "# START accepted. P%u %s %s target %u rpm, DShot %u.\r\n",
                   (unsigned int)(g_e2.profile_index + 1U),
                   profile->board_name,
                   profile->rpm_point,
                   (unsigned int)profile->target_rpm,
                   (unsigned int)profile->dshot_cmd);
    if ((len > 0) && (E2_UartWrite(line) != 0U))
    {
        g_e2.start_ack_pending = 0U;
    }
}

static void E2_SendStateChangeLine(E2_TestState_t state)
{
    const E2_Profile_t *profile = E2_GetSelectedProfile();
    char line[280];
    uint32_t t_ms;
    int len;

    if (state == STATE_WAIT_BT)
    {
        return;
    }

    t_ms = HAL_GetTick() - g_e2.test_start_tick;
    len = snprintf(line,
                   sizeof(line),
                   "# State %s at %lu ms. P%u %s %s target %u rpm, DShot %u, PB0 %s.\r\n",
                   E2_Test_GetStateName(state),
                   (unsigned long)t_ms,
                   (unsigned int)(g_e2.profile_index + 1U),
                   profile->board_name,
                   profile->rpm_point,
                   (unsigned int)profile->target_rpm,
                   (unsigned int)g_e2.active_dshot_value,
                   (state == STATE_RUN) ? "HIGH" : "LOW");
    if (len > 0)
    {
        E2_UartWrite(line);
    }
}

static uint8_t E2_UartWrite(const char *text)
{
    uint16_t free_space;
    uint32_t primask;
    size_t len;
    size_t i;

    if (text == NULL)
    {
        return 0U;
    }

    if (E2_UartCanSend() == 0U)
    {
        return 0U;
    }

    len = strlen(text);
    if ((len == 0U) || (len >= E2_UART_TX_BUFFER_SIZE))
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if (g_uart_tx_head >= g_uart_tx_tail)
    {
        free_space = (uint16_t)(E2_UART_TX_BUFFER_SIZE - (g_uart_tx_head - g_uart_tx_tail) - 1U);
    }
    else
    {
        free_space = (uint16_t)(g_uart_tx_tail - g_uart_tx_head - 1U);
    }

    if (len > free_space)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return 0U;
    }

    for (i = 0U; i < len; i++)
    {
        g_uart_tx_buffer[g_uart_tx_head] = (uint8_t)text[i];
        g_uart_tx_head = (uint16_t)((g_uart_tx_head + 1U) % E2_UART_TX_BUFFER_SIZE);
    }
    if (primask == 0U)
    {
        __enable_irq();
    }

    E2_UartTx_StartNext();
    return 1U;
}

static uint8_t E2_UartCanSend(void)
{
    if (HAL_GetTick() < E2_UART_BOOT_QUIET_MS)
    {
        return 0U;
    }

    return E2_IsBluetoothConnected();
}

static void E2_UartTx_StartNext(void)
{
    uint32_t primask;

    if (g_uart_tx_busy != 0U)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if (g_uart_tx_head == g_uart_tx_tail)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return;
    }

    g_uart_tx_byte = g_uart_tx_buffer[g_uart_tx_tail];
    g_uart_tx_tail = (uint16_t)((g_uart_tx_tail + 1U) % E2_UART_TX_BUFFER_SIZE);
    g_uart_tx_busy = 1U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    if (HAL_UART_Transmit_IT(&huart1, &g_uart_tx_byte, 1U) != HAL_OK)
    {
        g_uart_tx_busy = 0U;
    }
}

static void E2_UartTx_ResetQueue(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    g_uart_tx_head = 0U;
    g_uart_tx_tail = 0U;
    g_uart_tx_busy = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

static void E2_Button_Task(uint32_t now_ms)
{
    uint8_t raw_pressed;

    raw_pressed = (HAL_GPIO_ReadPin(BTN_THROTTLE_GPIO_Port, BTN_THROTTLE_Pin) == GPIO_PIN_RESET) ? 1U : 0U;

    if (raw_pressed != g_e2.button_raw_pressed)
    {
        g_e2.button_raw_pressed = raw_pressed;
        g_e2.button_change_tick = now_ms;
    }

    if ((now_ms - g_e2.button_change_tick) < E2_BUTTON_DEBOUNCE_MS)
    {
        return;
    }

    if (raw_pressed != g_e2.button_stable_pressed)
    {
        g_e2.button_stable_pressed = raw_pressed;

        if ((raw_pressed != 0U) &&
            (g_e2.state == STATE_WAIT_BT) &&
            (g_e2.bt_confirmed != 0U) &&
            (g_e2.start_command_received == 0U) &&
            (g_e2.start_armed_once == 0U))
        {
            E2_HandleThrottleButtonPress();
        }
    }
}

static void E2_HandleThrottleButtonPress(void)
{
    uint8_t next_profile = (uint8_t)(g_e2.profile_index + 1U);

    if (next_profile >= E2_PROFILE_COUNT)
    {
        next_profile = 0U;
    }

    E2_ApplyProfile(next_profile);
    E2_SendProfileLine("profile_set");
    g_e2.last_oled_update_tick = 0U;
    E2_Oled_Update(HAL_GetTick());
}

static const E2_Profile_t *E2_GetSelectedProfile(void)
{
    if (g_e2.profile_index >= E2_PROFILE_COUNT)
    {
        g_e2.profile_index = 0U;
    }

    return &g_e2_profiles[g_e2.profile_index];
}

static void E2_ApplyProfile(uint8_t profile_index)
{
    const E2_Profile_t *profile;

    if (profile_index >= E2_PROFILE_COUNT)
    {
        profile_index = 0U;
    }

    g_e2.profile_index = profile_index;
    profile = E2_GetSelectedProfile();
    g_e2.run_cmd_dshot = E2_ClampDshotThrottle(profile->dshot_cmd);
}

static void E2_SendProfileLine(const char *event)
{
    const E2_Profile_t *profile = E2_GetSelectedProfile();
    char line[192];
    int len;

    len = snprintf(line,
                   sizeof(line),
                   "# %s: P%u of %u, %s %s target %u rpm, DShot %u.\r\n",
                   (event != NULL) ? event : "profile",
                   (unsigned int)(g_e2.profile_index + 1U),
                   (unsigned int)E2_PROFILE_COUNT,
                   profile->board_name,
                   profile->rpm_point,
                   (unsigned int)profile->target_rpm,
                   (unsigned int)g_e2.run_cmd_dshot);
    if (len > 0)
    {
        E2_UartWrite(line);
    }
}

static HAL_StatusTypeDef E2_Oled_WriteCommand(uint8_t command)
{
    uint8_t frame[2];

    if (g_e2.oled_ready == 0U)
    {
        return HAL_ERROR;
    }

    frame[0] = 0x00U;
    frame[1] = command;
    return HAL_I2C_Master_Transmit(&hi2c1, E2_OLED_I2C_ADDR, frame, 2U, E2_OLED_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef E2_Oled_WriteData(const uint8_t *data, uint16_t size)
{
    uint8_t frame[129];

    if ((g_e2.oled_ready == 0U) || (data == NULL) || (size > 128U))
    {
        return HAL_ERROR;
    }

    frame[0] = 0x40U;
    memcpy(&frame[1], data, size);
    return HAL_I2C_Master_Transmit(&hi2c1, E2_OLED_I2C_ADDR, frame, (uint16_t)(size + 1U), E2_OLED_I2C_TIMEOUT_MS);
}

static void E2_Oled_Init(void)
{
    static const uint8_t init_cmds[] =
    {
        0xAEU, 0x20U, 0x02U, 0xB0U, 0xC8U, 0x00U, 0x10U, 0x40U,
        0x81U, 0x7FU, 0xA1U, 0xA6U, 0xA8U, 0x3FU, 0xA4U, 0xD3U,
        0x00U, 0xD5U, 0x80U, 0xD9U, 0xF1U, 0xDAU, 0x12U, 0xDBU,
        0x20U, 0x8DU, 0x14U, 0xAFU
    };
    uint8_t i;

    g_e2.oled_ready = 0U;

    if (HAL_I2C_IsDeviceReady(&hi2c1, E2_OLED_I2C_ADDR, 2U, 100U) != HAL_OK)
    {
        return;
    }

    g_e2.oled_ready = 1U;

    for (i = 0U; i < (uint8_t)(sizeof(init_cmds) / sizeof(init_cmds[0])); i++)
    {
        if (E2_Oled_WriteCommand(init_cmds[i]) != HAL_OK)
        {
            g_e2.oled_ready = 0U;
            return;
        }
    }

    E2_Oled_Clear();
}

static void E2_Oled_Service(uint32_t now_ms)
{
    if (g_e2.oled_ready != 0U)
    {
        return;
    }

    if (g_e2.last_oled_init_attempt_tick == 0U)
    {
        if (now_ms < E2_OLED_POWERUP_DELAY_MS)
        {
            return;
        }
    }
    else if ((now_ms - g_e2.last_oled_init_attempt_tick) < E2_OLED_RETRY_INTERVAL_MS)
    {
        return;
    }

    g_e2.last_oled_init_attempt_tick = now_ms;
    E2_Oled_Init();

    if (g_e2.oled_ready != 0U)
    {
        g_e2.last_oled_update_tick = 0U;
        E2_Oled_Update(now_ms);
    }
}

static void E2_Oled_Clear(void)
{
    if (g_e2.oled_ready == 0U)
    {
        return;
    }

    memset(g_oled_buffer, 0, sizeof(g_oled_buffer));
    E2_Oled_Flush();
}

static void E2_Oled_Flush(void)
{
    uint8_t page;

    if (g_e2.oled_ready == 0U)
    {
        return;
    }

    for (page = 0U; page < 8U; page++)
    {
        if ((E2_Oled_WriteCommand((uint8_t)(0xB0U | page)) != HAL_OK) ||
            (E2_Oled_WriteCommand((uint8_t)(0x00U | (E2_OLED_COLUMN_OFFSET & 0x0FU))) != HAL_OK) ||
            (E2_Oled_WriteCommand((uint8_t)(0x10U | ((E2_OLED_COLUMN_OFFSET >> 4U) & 0x0FU))) != HAL_OK) ||
            (E2_Oled_WriteData(&g_oled_buffer[page * 128U], 128U) != HAL_OK))
        {
            g_e2.oled_ready = 0U;
            g_e2.last_oled_init_attempt_tick = HAL_GetTick();
            return;
        }
    }
}

static void E2_Oled_Update(uint32_t now_ms)
{
    const E2_Profile_t *profile = E2_GetSelectedProfile();
    char line0[32];
    char line1[32];
    char line2[32];
    char line3[32];
    char line4[32];
    uint32_t prepare_elapsed_ms;
    uint32_t prepare_remaining_s;
    uint32_t run_elapsed_ms;
    uint32_t run_elapsed_s;
    uint32_t ramp_elapsed_ms;
    uint32_t ramp_remaining_s;
    uint32_t stabilize_elapsed_ms;
    uint32_t stabilize_remaining_s;

    if (g_e2.oled_ready == 0U)
    {
        return;
    }

    if ((g_e2.last_oled_update_tick != 0U) &&
        ((now_ms - g_e2.last_oled_update_tick) < E2_OLED_UPDATE_INTERVAL_MS))
    {
        return;
    }

    g_e2.last_oled_update_tick = now_ms;
    memset(g_oled_buffer, 0, sizeof(g_oled_buffer));

    (void)snprintf(line0, sizeof(line0), "E2 WAVE");
    (void)snprintf(line1, sizeof(line1), "W PHASE");
    (void)snprintf(line2, sizeof(line2), "PB0 MARK");

    if (g_e2.state == STATE_PREPARE)
    {
        prepare_elapsed_ms = now_ms - g_e2.state_enter_tick;
        if (prepare_elapsed_ms >= E2_BT_PREPARE_MS)
        {
            prepare_remaining_s = 0U;
        }
        else
        {
            prepare_remaining_s = (E2_BT_PREPARE_MS - prepare_elapsed_ms + 999U) / 1000U;
        }
    }
    else
    {
        prepare_remaining_s = 0U;
    }

    if (g_e2.state == STATE_RAMP)
    {
        ramp_elapsed_ms = now_ms - g_e2.state_enter_tick;
        if (ramp_elapsed_ms >= E2_RAMP_MS)
        {
            ramp_remaining_s = 0U;
        }
        else
        {
            ramp_remaining_s = (E2_RAMP_MS - ramp_elapsed_ms + 999U) / 1000U;
        }
    }
    else
    {
        ramp_elapsed_ms = 0U;
        ramp_remaining_s = 0U;
    }

    if (g_e2.state == STATE_STABILIZE)
    {
        stabilize_elapsed_ms = now_ms - g_e2.state_enter_tick;
        if (stabilize_elapsed_ms >= E2_STABILIZE_MS)
        {
            stabilize_remaining_s = 0U;
        }
        else
        {
            stabilize_remaining_s = (E2_STABILIZE_MS - stabilize_elapsed_ms + 999U) / 1000U;
        }
    }
    else
    {
        stabilize_remaining_s = 0U;
    }

    if (g_e2.state == STATE_RUN)
    {
        run_elapsed_ms = now_ms - g_e2.state_enter_tick;
    }
    else if ((g_e2.state == STATE_DONE) || (g_e2.state == STATE_IDLE))
    {
        run_elapsed_ms = E2_RUN_MS;
    }
    else
    {
        run_elapsed_ms = 0U;
    }

    run_elapsed_s = run_elapsed_ms / 1000U;
    if (g_e2.state == STATE_RAMP)
    {
        (void)snprintf(line3,
                       sizeof(line3),
                       "P%u %s%s %u-%u",
                       (unsigned int)(g_e2.profile_index + 1U),
                       profile->oled_name,
                       profile->rpm_point,
                       (unsigned int)g_e2.active_dshot_value,
                       (unsigned int)g_e2.run_cmd_dshot);
    }
    else
    {
        (void)snprintf(line3,
                       sizeof(line3),
                       "P%u %s%s CMD %u",
                       (unsigned int)(g_e2.profile_index + 1U),
                       profile->oled_name,
                       profile->rpm_point,
                       (unsigned int)g_e2.run_cmd_dshot);
    }
    if (g_e2.state == STATE_WAIT_BT)
    {
        (void)snprintf(line4, sizeof(line4), "%s", (g_e2.bt_confirmed != 0U) ? "WAIT START" : "WAIT BT");
    }
    else if (g_e2.state == STATE_PREPARE)
    {
        (void)snprintf(line4, sizeof(line4), "PREP %2luS", (unsigned long)prepare_remaining_s);
    }
    else if (g_e2.state == STATE_RAMP)
    {
        (void)snprintf(line4, sizeof(line4), "RAMP %2luS", (unsigned long)ramp_remaining_s);
    }
    else if (g_e2.state == STATE_STABILIZE)
    {
        (void)snprintf(line4, sizeof(line4), "STAB %2luS", (unsigned long)stabilize_remaining_s);
    }
    else if (g_e2.state == STATE_RUN)
    {
        (void)snprintf(line4, sizeof(line4), "CAP %3luS", (unsigned long)run_elapsed_s);
    }
    else
    {
        (void)snprintf(line4, sizeof(line4), "DONE");
    }

    E2_Oled_DrawLine(0U, line0);
    E2_Oled_DrawLine(1U, line1);
    E2_Oled_DrawLine(2U, line2);
    E2_Oled_DrawLine(3U, line3);
    E2_Oled_DrawLine(4U, line4);
    E2_Oled_Flush();
}

static void E2_Oled_DrawLine(uint8_t line_index, const char *text)
{
    uint8_t x = 0U;
    uint8_t y;

    if ((g_e2.oled_ready == 0U) || (line_index >= 5U))
    {
        return;
    }

    y = (uint8_t)(line_index * 11U);

    while ((text != NULL) && (*text != '\0') && (x <= 120U))
    {
        E2_Oled_DrawChar(x, y, *text);
        x = (uint8_t)(x + 7U);
        text++;
    }
}

static void E2_Oled_DrawChar(uint8_t x, uint8_t y, char c)
{
    uint8_t glyph[5];
    uint8_t col;
    uint8_t row;

    E2_Oled_GetGlyph5x7(c, glyph);

    for (col = 0U; col < 5U; col++)
    {
        for (row = 0U; row < 7U; row++)
        {
            if ((glyph[col] & (uint8_t)(1U << row)) != 0U)
            {
                E2_Oled_SetPixel((uint8_t)(x + col), (uint8_t)(y + row), 1U);
                E2_Oled_SetPixel((uint8_t)(x + col + 1U), (uint8_t)(y + row), 1U);
            }
        }
    }
}

static void E2_Oled_SetPixel(uint8_t x, uint8_t y, uint8_t on)
{
    uint16_t index;
    uint8_t mask;

    if ((x >= 128U) || (y >= 64U))
    {
        return;
    }

    index = (uint16_t)((y / 8U) * 128U + x);
    mask = (uint8_t)(1U << (y % 8U));

    if (on != 0U)
    {
        g_oled_buffer[index] |= mask;
    }
    else
    {
        g_oled_buffer[index] &= (uint8_t)(~mask);
    }
}

static void E2_Oled_GetGlyph5x7(char c, uint8_t glyph[5])
{
    if (glyph == NULL)
    {
        return;
    }

    memset(glyph, 0, 5U);

    switch (c)
    {
    case '-': glyph[0] = 0x08U; glyph[1] = 0x08U; glyph[2] = 0x08U; glyph[3] = 0x08U; glyph[4] = 0x08U; break;
    case '.': glyph[1] = 0x60U; glyph[2] = 0x60U; break;
    case '0': glyph[0] = 0x3EU; glyph[1] = 0x51U; glyph[2] = 0x49U; glyph[3] = 0x45U; glyph[4] = 0x3EU; break;
    case '1': glyph[0] = 0x00U; glyph[1] = 0x42U; glyph[2] = 0x7FU; glyph[3] = 0x40U; glyph[4] = 0x00U; break;
    case '2': glyph[0] = 0x42U; glyph[1] = 0x61U; glyph[2] = 0x51U; glyph[3] = 0x49U; glyph[4] = 0x46U; break;
    case '3': glyph[0] = 0x21U; glyph[1] = 0x41U; glyph[2] = 0x45U; glyph[3] = 0x4BU; glyph[4] = 0x31U; break;
    case '4': glyph[0] = 0x18U; glyph[1] = 0x14U; glyph[2] = 0x12U; glyph[3] = 0x7FU; glyph[4] = 0x10U; break;
    case '5': glyph[0] = 0x27U; glyph[1] = 0x45U; glyph[2] = 0x45U; glyph[3] = 0x45U; glyph[4] = 0x39U; break;
    case '6': glyph[0] = 0x3CU; glyph[1] = 0x4AU; glyph[2] = 0x49U; glyph[3] = 0x49U; glyph[4] = 0x30U; break;
    case '7': glyph[0] = 0x01U; glyph[1] = 0x71U; glyph[2] = 0x09U; glyph[3] = 0x05U; glyph[4] = 0x03U; break;
    case '8': glyph[0] = 0x36U; glyph[1] = 0x49U; glyph[2] = 0x49U; glyph[3] = 0x49U; glyph[4] = 0x36U; break;
    case '9': glyph[0] = 0x06U; glyph[1] = 0x49U; glyph[2] = 0x49U; glyph[3] = 0x29U; glyph[4] = 0x1EU; break;
    case 'A': glyph[0] = 0x7EU; glyph[1] = 0x11U; glyph[2] = 0x11U; glyph[3] = 0x11U; glyph[4] = 0x7EU; break;
    case 'B': glyph[0] = 0x7FU; glyph[1] = 0x49U; glyph[2] = 0x49U; glyph[3] = 0x49U; glyph[4] = 0x36U; break;
    case 'C': glyph[0] = 0x3EU; glyph[1] = 0x41U; glyph[2] = 0x41U; glyph[3] = 0x41U; glyph[4] = 0x22U; break;
    case 'D': glyph[0] = 0x7FU; glyph[1] = 0x41U; glyph[2] = 0x41U; glyph[3] = 0x22U; glyph[4] = 0x1CU; break;
    case 'E': glyph[0] = 0x7FU; glyph[1] = 0x49U; glyph[2] = 0x49U; glyph[3] = 0x49U; glyph[4] = 0x41U; break;
    case 'F': glyph[0] = 0x7FU; glyph[1] = 0x09U; glyph[2] = 0x09U; glyph[3] = 0x09U; glyph[4] = 0x01U; break;
    case 'G': glyph[0] = 0x3EU; glyph[1] = 0x41U; glyph[2] = 0x49U; glyph[3] = 0x49U; glyph[4] = 0x7AU; break;
    case 'H': glyph[0] = 0x7FU; glyph[1] = 0x08U; glyph[2] = 0x08U; glyph[3] = 0x08U; glyph[4] = 0x7FU; break;
    case 'I': glyph[0] = 0x00U; glyph[1] = 0x41U; glyph[2] = 0x7FU; glyph[3] = 0x41U; glyph[4] = 0x00U; break;
    case 'J': glyph[0] = 0x20U; glyph[1] = 0x40U; glyph[2] = 0x41U; glyph[3] = 0x3FU; glyph[4] = 0x01U; break;
    case 'K': glyph[0] = 0x7FU; glyph[1] = 0x08U; glyph[2] = 0x14U; glyph[3] = 0x22U; glyph[4] = 0x41U; break;
    case 'L': glyph[0] = 0x7FU; glyph[1] = 0x40U; glyph[2] = 0x40U; glyph[3] = 0x40U; glyph[4] = 0x40U; break;
    case 'M': glyph[0] = 0x7FU; glyph[1] = 0x02U; glyph[2] = 0x0CU; glyph[3] = 0x02U; glyph[4] = 0x7FU; break;
    case 'N': glyph[0] = 0x7FU; glyph[1] = 0x04U; glyph[2] = 0x08U; glyph[3] = 0x10U; glyph[4] = 0x7FU; break;
    case 'O': glyph[0] = 0x3EU; glyph[1] = 0x41U; glyph[2] = 0x41U; glyph[3] = 0x41U; glyph[4] = 0x3EU; break;
    case 'P': glyph[0] = 0x7FU; glyph[1] = 0x09U; glyph[2] = 0x09U; glyph[3] = 0x09U; glyph[4] = 0x06U; break;
    case 'Q': glyph[0] = 0x3EU; glyph[1] = 0x41U; glyph[2] = 0x51U; glyph[3] = 0x21U; glyph[4] = 0x5EU; break;
    case 'R': glyph[0] = 0x7FU; glyph[1] = 0x09U; glyph[2] = 0x19U; glyph[3] = 0x29U; glyph[4] = 0x46U; break;
    case 'S': glyph[0] = 0x46U; glyph[1] = 0x49U; glyph[2] = 0x49U; glyph[3] = 0x49U; glyph[4] = 0x31U; break;
    case 'T': glyph[0] = 0x01U; glyph[1] = 0x01U; glyph[2] = 0x7FU; glyph[3] = 0x01U; glyph[4] = 0x01U; break;
    case 'U': glyph[0] = 0x3FU; glyph[1] = 0x40U; glyph[2] = 0x40U; glyph[3] = 0x40U; glyph[4] = 0x3FU; break;
    case 'V': glyph[0] = 0x1FU; glyph[1] = 0x20U; glyph[2] = 0x40U; glyph[3] = 0x20U; glyph[4] = 0x1FU; break;
    case 'W': glyph[0] = 0x7FU; glyph[1] = 0x20U; glyph[2] = 0x18U; glyph[3] = 0x20U; glyph[4] = 0x7FU; break;
    case 'X': glyph[0] = 0x63U; glyph[1] = 0x14U; glyph[2] = 0x08U; glyph[3] = 0x14U; glyph[4] = 0x63U; break;
    case 'Y': glyph[0] = 0x07U; glyph[1] = 0x08U; glyph[2] = 0x70U; glyph[3] = 0x08U; glyph[4] = 0x07U; break;
    case 'Z': glyph[0] = 0x61U; glyph[1] = 0x51U; glyph[2] = 0x49U; glyph[3] = 0x45U; glyph[4] = 0x43U; break;
    default: break;
    }
}

static void E2_StatusLed_Set(uint8_t on)
{
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port,
                      STATUS_LED_Pin,
                      (on != 0U) ? E2_STATUS_LED_ON_LEVEL : E2_STATUS_LED_OFF_LEVEL);
}

static void E2_ScopeMarker_Set(uint8_t high)
{
    HAL_GPIO_WritePin(SCOPE_MARKER_GPIO_Port,
                      SCOPE_MARKER_Pin,
                      (high != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void E2_StatusLed_Update(uint32_t now_ms)
{
    uint32_t phase_ms;

    switch (g_e2.state)
    {
    case STATE_WAIT_BT:
        phase_ms = now_ms % 2000U;
        E2_StatusLed_Set(((phase_ms < 80U) || ((phase_ms >= 250U) && (phase_ms < 330U))) ? 1U : 0U);
        break;

    case STATE_PREPARE:
        phase_ms = now_ms % 1000U;
        E2_StatusLed_Set((phase_ms < 120U) ? 1U : 0U);
        break;

    case STATE_RAMP:
        phase_ms = now_ms % 500U;
        E2_StatusLed_Set((phase_ms < 250U) ? 1U : 0U);
        break;

    case STATE_STABILIZE:
        phase_ms = now_ms % 250U;
        E2_StatusLed_Set((phase_ms < 125U) ? 1U : 0U);
        break;

    case STATE_RUN:
        E2_StatusLed_Set(1U);
        break;

    case STATE_DONE:
    default:
        E2_StatusLed_Set(0U);
        break;

    case STATE_IDLE:
        E2_StatusLed_Set(0U);
        break;
    }
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM4)
    {
        return;
    }

    (void)HAL_TIM_PWM_Stop_DMA(htim, TIM_CHANNEL_3);
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_3, 0U);
    __HAL_TIM_SET_COUNTER(htim, 0U);
    g_e2.dshot_dma_busy = 0U;
}

static uint16_t E2_MapUsToDshot(uint16_t us)
{
    uint32_t scaled;

    if (us <= E2_PWM_MIN_US)
    {
        return 0U;
    }

    if (us >= E2_PWM_MAX_US)
    {
        return E2_DSHOT_MAX_THROTTLE;
    }

    scaled = (uint32_t)(us - (E2_PWM_MIN_US + 1U)) * (uint32_t)(E2_DSHOT_MAX_THROTTLE - E2_DSHOT_MIN_THROTTLE);
    scaled /= (uint32_t)(E2_PWM_MAX_US - (E2_PWM_MIN_US + 1U));

    return (uint16_t)(E2_DSHOT_MIN_THROTTLE + scaled);
}

static uint16_t E2_ClampDshotThrottle(uint16_t cmd)
{
    if (cmd == 0U)
    {
        return 0U;
    }

    if (cmd < E2_DSHOT_MIN_THROTTLE)
    {
        return E2_DSHOT_MIN_THROTTLE;
    }

    if (cmd > E2_DSHOT_MAX_THROTTLE)
    {
        return E2_DSHOT_MAX_THROTTLE;
    }

    return cmd;
}

static uint16_t E2_Dshot_BuildPacket(uint16_t throttle_value)
{
    uint16_t packet;
    uint16_t checksum;
    uint16_t csum_data;
    uint8_t i;

    packet = (uint16_t)((throttle_value << 1U) | (E2_DSHOT_TELEMETRY_BIT & 0x1U));
    csum_data = packet;
    checksum = 0U;

    for (i = 0U; i < 3U; i++)
    {
        checksum ^= (uint16_t)(csum_data & 0xFU);
        csum_data >>= 4U;
    }

    checksum &= 0xFU;
    return (uint16_t)((packet << 4U) | checksum);
}

static void E2_Dshot_PrepareFrame(uint16_t throttle_value)
{
    uint16_t packet;
    uint8_t i;

    packet = E2_Dshot_BuildPacket(throttle_value);

    for (i = 0U; i < E2_DSHOT_FRAME_BITS; i++)
    {
        g_dshot_dma_buffer[i] = ((packet & 0x8000U) != 0U) ? E2_DSHOT_BIT_1_HIGH_TICKS : E2_DSHOT_BIT_0_HIGH_TICKS;
        packet <<= 1U;
    }

    for (i = E2_DSHOT_FRAME_BITS; i < E2_DSHOT_DMA_BUFFER_LENGTH; i++)
    {
        g_dshot_dma_buffer[i] = 0U;
    }
}

static void E2_Dshot_TriggerFrame(uint16_t throttle_value)
{
    HAL_StatusTypeDef status;

    if (g_e2.dshot_dma_busy != 0U)
    {
        return;
    }

    E2_Dshot_PrepareFrame(throttle_value);
    g_e2.dshot_dma_busy = 1U;

    __HAL_TIM_DISABLE(&htim4);
    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, g_dshot_dma_buffer[0]);

    status = HAL_TIM_PWM_Start_DMA(&htim4,
                                   TIM_CHANNEL_3,
                                   (const uint32_t *)&g_dshot_dma_buffer[1],
                                   (uint16_t)(E2_DSHOT_DMA_BUFFER_LENGTH - 1U));
    if (status != HAL_OK)
    {
        __HAL_TIM_DISABLE(&htim4);
        __HAL_TIM_SET_COUNTER(&htim4, 0U);
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);
        g_e2.dshot_dma_busy = 0U;
        set_throttle_command(0U);
        g_e2.state = STATE_DONE;
        return;
    }
}

static void E2_ForceMotorStopFrame(void)
{
    set_throttle_command(0U);
    E2_ScopeMarker_Set(0U);

    if (g_e2.dshot_dma_busy != 0U)
    {
        (void)HAL_TIM_PWM_Stop_DMA(&htim4, TIM_CHANNEL_3);
        g_e2.dshot_dma_busy = 0U;
    }

    __HAL_TIM_DISABLE(&htim4);
    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);
    E2_Dshot_TriggerFrame(0U);
}

static void E2_Dshot_Service(uint32_t now_ms)
{
    if ((now_ms - g_e2.last_dshot_send_tick) < E2_DSHOT_SEND_INTERVAL_MS)
    {
        return;
    }

    if (g_e2.dshot_dma_busy != 0U)
    {
        return;
    }

    g_e2.last_dshot_send_tick = now_ms;
    E2_Dshot_TriggerFrame(g_e2.active_dshot_value);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        g_uart_rx_active = 0U;
        E2_ProcessReceivedByte(g_uart_rx_byte);
        E2_UartRx_Start();
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        g_uart_tx_busy = 0U;
        E2_UartTx_StartNext();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        g_uart_rx_active = 0U;
        g_uart_tx_busy = 0U;
        E2_UartTx_ResetQueue();
        E2_UartRx_Start();
    }
}

static void E2_UartRx_Start(void)
{
    if (g_uart_rx_active != 0U)
    {
        return;
    }

    if (HAL_UART_Receive_IT(&huart1, &g_uart_rx_byte, 1U) == HAL_OK)
    {
        g_uart_rx_active = 1U;
    }
    else
    {
        __HAL_UART_CLEAR_OREFLAG(&huart1);
        g_uart_rx_active = 0U;
    }
}

static void E2_ProcessReceivedByte(uint8_t byte)
{
    char c = (char)byte;

    if ((c == '\r') || (c == '\n'))
    {
        if (g_uart_cmd_index > 0U)
        {
            g_uart_cmd_buffer[g_uart_cmd_index] = '\0';
            if ((g_e2.bt_confirmed != 0U) &&
                (E2_IsStopCommand(g_uart_cmd_buffer) != 0U))
            {
                E2_ProcessStopCommand();
            }
            else if ((g_e2.state == STATE_WAIT_BT) &&
                     (g_e2.bt_confirmed != 0U) &&
                     (E2_IsStartCommand(g_uart_cmd_buffer) != 0U))
            {
                E2_ProcessStartCommand();
            }
            else if ((g_e2.state == STATE_WAIT_BT) &&
                     (g_e2.bt_confirmed != 0U) &&
                     (g_e2.start_command_received == 0U) &&
                     (g_e2.start_armed_once == 0U))
            {
                (void)E2_TryApplyProfileCommand(g_uart_cmd_buffer);
            }
            g_uart_cmd_index = 0U;
        }
        return;
    }

    if ((c >= 'a') && (c <= 'z'))
    {
        c = (char)(c - 'a' + 'A');
    }

    if (((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_'))
    {
        if (g_uart_cmd_index < (uint8_t)(sizeof(g_uart_cmd_buffer) - 1U))
        {
            g_uart_cmd_buffer[g_uart_cmd_index++] = c;
            g_uart_cmd_buffer[g_uart_cmd_index] = '\0';

            if ((g_e2.bt_confirmed != 0U) &&
                (E2_IsStopCommand(g_uart_cmd_buffer) != 0U))
            {
                E2_ProcessStopCommand();
                g_uart_cmd_index = 0U;
            }
            else if ((g_e2.state == STATE_WAIT_BT) &&
                     (g_e2.bt_confirmed != 0U) &&
                     (E2_IsStartCommand(g_uart_cmd_buffer) != 0U))
            {
                E2_ProcessStartCommand();
                g_uart_cmd_index = 0U;
            }
            else if ((g_e2.state == STATE_WAIT_BT) &&
                     (g_e2.bt_confirmed != 0U) &&
                     (g_e2.start_command_received == 0U) &&
                     (g_e2.start_armed_once == 0U) &&
                     (g_uart_cmd_index == 2U) &&
                     (g_uart_cmd_buffer[0] == 'P') &&
                     (g_uart_cmd_buffer[1] >= '1') &&
                     (g_uart_cmd_buffer[1] <= '6'))
            {
                (void)E2_TryApplyProfileCommand(g_uart_cmd_buffer);
                g_uart_cmd_index = 0U;
            }
        }
        else
        {
            g_uart_cmd_index = 0U;
        }
    }
    else
    {
        g_uart_cmd_index = 0U;
    }
}

static uint8_t E2_IsStartCommand(const char *text)
{
    if (text == NULL)
    {
        return 0U;
    }

    return (strcmp(text, "START") == 0) ? 1U : 0U;
}

static uint8_t E2_IsStopCommand(const char *text)
{
    if (text == NULL)
    {
        return 0U;
    }

    return ((strcmp(text, "STOP") == 0) || (strcmp(text, "ABORT") == 0)) ? 1U : 0U;
}

static uint8_t E2_TryApplyProfileCommand(const char *text)
{
    uint8_t profile_index = 0xFFU;

    if (text == NULL)
    {
        return 0U;
    }

    if (((strcmp(text, "P1") == 0) || (strcmp(text, "PROFILE1") == 0) || (strcmp(text, "SI_R1") == 0)))
    {
        profile_index = 0U;
    }
    else if (((strcmp(text, "P2") == 0) || (strcmp(text, "PROFILE2") == 0) || (strcmp(text, "SI_R2") == 0)))
    {
        profile_index = 1U;
    }
    else if (((strcmp(text, "P3") == 0) || (strcmp(text, "PROFILE3") == 0) || (strcmp(text, "SI_R3") == 0)))
    {
        profile_index = 2U;
    }
    else if (((strcmp(text, "P4") == 0) || (strcmp(text, "PROFILE4") == 0) || (strcmp(text, "GAN_R1") == 0)))
    {
        profile_index = 3U;
    }
    else if (((strcmp(text, "P5") == 0) || (strcmp(text, "PROFILE5") == 0) || (strcmp(text, "GAN_R2") == 0)))
    {
        profile_index = 4U;
    }
    else if (((strcmp(text, "P6") == 0) || (strcmp(text, "PROFILE6") == 0) || (strcmp(text, "GAN_R3") == 0)))
    {
        profile_index = 5U;
    }

    if (profile_index >= E2_PROFILE_COUNT)
    {
        return 0U;
    }

    E2_ApplyProfile(profile_index);
    E2_SendProfileLine("profile_set");
    g_e2.last_oled_update_tick = 0U;
    E2_Oled_Update(HAL_GetTick());
    return 1U;
}

static void E2_ProcessStartCommand(void)
{
    if (g_e2.start_armed_once == 0U)
    {
        g_e2.start_command_received = 1U;
        g_e2.start_ack_pending = 1U;
        g_e2.start_armed_once = 1U;
    }
}

static void E2_ProcessStopCommand(void)
{
    char line[96];
    uint32_t now_ms = HAL_GetTick();
    uint32_t t_ms = now_ms - g_e2.test_start_tick;
    int len;

    E2_ForceMotorStopFrame();
    g_e2.start_command_received = 0U;
    g_e2.start_ack_pending = 0U;
    g_e2.start_armed_once = 0U;
    if ((g_e2.state == STATE_PREPARE) || (g_e2.state == STATE_RAMP) ||
        (g_e2.state == STATE_STABILIZE) || (g_e2.state == STATE_RUN))
    {
        g_e2.state = STATE_DONE;
        g_e2.state_enter_tick = now_ms;
        E2_SendStateChangeLine(STATE_DONE);
    }

    len = snprintf(line,
                   sizeof(line),
                   "# STOP accepted at %lu ms. DShot stop sent.\r\n",
                   (unsigned long)t_ms);
    if (len > 0)
    {
        E2_UartWrite(line);
    }
}


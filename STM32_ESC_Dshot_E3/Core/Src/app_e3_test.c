#include "app_e3_test.h"

#include <stdio.h>
#include <string.h>

#include "adc.h"
#include "app_button.h"
#include "gpio.h"
#include "i2c.h"
#include "tim.h"

#define H1_ADC_CHANNEL_COUNT              2U
#define H1_ADC_DMA_SAMPLES_PER_CHANNEL    64U
#define H1_ADC_DMA_BUFFER_LENGTH          (H1_ADC_CHANNEL_COUNT * H1_ADC_DMA_SAMPLES_PER_CHANNEL)

#define H1_DSHOT_FRAME_BITS               16U
#define H1_DSHOT_RESET_SLOTS              8U
#define H1_DSHOT_DMA_BUFFER_LENGTH        (H1_DSHOT_FRAME_BITS + H1_DSHOT_RESET_SLOTS)
#define H1_DSHOT_BIT_0_HIGH_TICKS         90U
#define H1_DSHOT_BIT_1_HIGH_TICKS         180U

#define H1_STATUS_LED_ON_LEVEL            GPIO_PIN_RESET
#define H1_STATUS_LED_OFF_LEVEL           GPIO_PIN_SET

#define E3_PROFILE_COUNT                  6U
#define E3_CHECKPOINT_COUNT               5U

typedef struct
{
    const char *board_name;
    const char *oled_name;
    const char *rpm_point;
    uint16_t target_rpm;
    uint16_t dshot_cmd;
} E3_Profile_t;

static const E3_Profile_t g_e3_profiles[E3_PROFILE_COUNT] =
{
    { "Si",  "SI",  "R1", E3_P0_TARGET_R1_RPM, E3_P0_SI_R1_DSHOT  },
    { "Si",  "SI",  "R2", E3_P0_TARGET_R2_RPM, E3_P0_SI_R2_DSHOT  },
    { "Si",  "SI",  "R3", E3_P0_TARGET_R3_RPM, E3_P0_SI_R3_DSHOT  },
    { "GaN", "GAN", "R1", E3_P0_TARGET_R1_RPM, E3_P0_GAN_R1_DSHOT },
    { "GaN", "GAN", "R2", E3_P0_TARGET_R2_RPM, E3_P0_GAN_R2_DSHOT },
    { "GaN", "GAN", "R3", E3_P0_TARGET_R3_RPM, E3_P0_GAN_R3_DSHOT }
};

static const uint32_t g_e3_checkpoint_ms[E3_CHECKPOINT_COUNT] =
{
    30000U,
    60000U,
    120000U,
    300000U,
    600000U
};

typedef struct
{
    H1_TestState_t state;
    uint32_t test_start_tick;
    uint32_t state_enter_tick;
    uint32_t last_dshot_send_tick;
    uint32_t last_zero_offset_sample_tick;
    uint32_t last_oled_update_tick;
    uint32_t last_oled_init_attempt_tick;
    uint32_t heat_soak_elapsed_ms;
    uint32_t ramp_start_tick;
    uint16_t current_cmd_us;
    uint16_t current_dshot_value;
    uint16_t profile_dshot;
    uint16_t ramp_start_dshot;
    uint16_t ramp_target_dshot;
    uint8_t profile_index;
    uint8_t next_checkpoint_index;
    uint32_t zero_offset_sample_count;
    uint32_t trigger_release_tick;
    float zero_offset_sum_v;
    float baseline_zero_offset_voltage;
    float active_zero_offset_voltage;
    volatile uint8_t dshot_dma_busy;
    uint8_t oled_ready;
    uint8_t trigger_active;
    uint8_t abort_button_latched;
    AppButton_Handle_t button;
    H1_AdcProcessed_t adc;
} H1_TestContext_t;

static H1_TestContext_t g_h1;
static volatile uint16_t g_adc_dma_buffer[H1_ADC_DMA_BUFFER_LENGTH];
static uint16_t g_dshot_dma_buffer[H1_DSHOT_DMA_BUFFER_LENGTH];
static uint8_t g_oled_buffer[128U * 8U];

static void H1_Test_EnterState(H1_TestState_t new_state);
static void H1_StartSession(uint32_t now_ms);
static void H1_Button_Service(uint32_t now_ms);
static void H1_HandleButtonEvent(AppButton_Event_t event, uint32_t now_ms);
static uint8_t H1_IsAbortableState(H1_TestState_t state);
static void H1_SelectNextProfile(void);
static void H1_RequestAbort(void);
static void H1_ForceMotorStopFrame(void);
static void H1_LoadProfile(uint8_t profile_index);
static uint16_t H1_GetRampStartDshot(void);
static void H1_UpdateRampThrottle(uint32_t now_ms);
static void H1_HeatSoak_Service(uint32_t now_ms, uint32_t state_elapsed_ms);
static int32_t H1_GetActiveCheckpointMs(uint32_t run_elapsed_ms);
static void H1_FormatFloat3(char *dst, size_t len, float value);
static void H1_StatusLed_Set(uint8_t on);
static void H1_StatusLed_Update(uint32_t now_ms);
static void H1_Trigger_Set(uint8_t high);
static void H1_Trigger_Pulse(uint32_t now_ms);
static void H1_Trigger_Service(uint32_t now_ms);
static uint16_t H1_ClampDshotThrottle(uint16_t cmd);
static uint16_t H1_Dshot_BuildPacket(uint16_t throttle_value);
static void H1_Dshot_PrepareFrame(uint16_t throttle_value);
static void H1_Dshot_TriggerFrame(uint16_t throttle_value);
static void H1_Dshot_Service(uint32_t now_ms);
static void H1_UpdateCurrentDerived(H1_AdcProcessed_t *adc_data);
static HAL_StatusTypeDef H1_Oled_WriteCommand(uint8_t command);
static HAL_StatusTypeDef H1_Oled_WriteData(const uint8_t *data, uint16_t size);
static void H1_Oled_Init(void);
static void H1_Oled_Service(uint32_t now_ms);
static void H1_Oled_Clear(void);
static void H1_Oled_Flush(void);
static void H1_Oled_Update(uint32_t now_ms);
static void H1_Oled_DrawLine(uint8_t page, const char *text);
static void H1_Oled_DrawChar(uint8_t x, uint8_t y, char c);
static void H1_Oled_SetPixel(uint8_t x, uint8_t y, uint8_t on);
static void H1_Oled_GetGlyph5x7(char c, uint8_t glyph[5]);

void E3_Test_Init(void)
{
    memset(&g_h1, 0, sizeof(g_h1));

    g_h1.test_start_tick = HAL_GetTick();
    g_h1.state_enter_tick = g_h1.test_start_tick;
    g_h1.state = STATE_ARMING;
    g_h1.current_cmd_us = 0U;
    g_h1.current_dshot_value = 0U;
    g_h1.baseline_zero_offset_voltage = CURRENT_OFFSET_V;
    g_h1.active_zero_offset_voltage = CURRENT_OFFSET_V;
    H1_LoadProfile(0U);
    AppButton_Init(&g_h1.button,
                   BTN_THROTTLE_GPIO_Port,
                   BTN_THROTTLE_Pin,
                   GPIO_PIN_RESET,
                   E3_BUTTON_DEBOUNCE_MS,
                   E3_BUTTON_LONG_PRESS_MS);

    set_throttle_command(0U);
    H1_Trigger_Set(0U);

    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);

    H1_ForceMotorStopFrame();

    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_dma_buffer, H1_ADC_DMA_BUFFER_LENGTH) != HAL_OK)
    {
        Error_Handler();
    }

    HAL_Delay(E3_OLED_POWERUP_DELAY_MS);
    H1_Oled_Service(HAL_GetTick());
    H1_StatusLed_Update(HAL_GetTick());
}

void E3_Test_Task(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint32_t state_elapsed_ms;

    process_adc_average(&g_h1.adc);
    update_zero_offset(&g_h1.adc, now_ms);
    H1_UpdateCurrentDerived(&g_h1.adc);
    H1_UpdateRampThrottle(now_ms);
    H1_Dshot_Service(now_ms);
    H1_Trigger_Service(now_ms);
    H1_StatusLed_Update(now_ms);
    H1_Button_Service(now_ms);
    H1_Oled_Service(now_ms);
    H1_Oled_Update(now_ms);

    state_elapsed_ms = now_ms - g_h1.state_enter_tick;

    switch (g_h1.state)
    {
    case STATE_ARMING:
        if (state_elapsed_ms >= E3_ESC_ARMING_MS)
        {
            H1_Test_EnterState(STATE_READY);
        }
        break;

    case STATE_READY:
        break;

    case STATE_PREPARE:
        if (state_elapsed_ms >= E3_PREPARE_MS)
        {
            H1_Test_EnterState(STATE_RAMP);
        }
        break;

    case STATE_RAMP:
        if (state_elapsed_ms >= E3_RAMP_MS)
        {
            H1_Test_EnterState(STATE_STABILIZE);
        }
        break;

    case STATE_STABILIZE:
        if (state_elapsed_ms >= E3_STABILIZE_MS)
        {
            H1_Test_EnterState(STATE_HEAT_SOAK);
        }
        break;

    case STATE_HEAT_SOAK:
        H1_HeatSoak_Service(now_ms, state_elapsed_ms);
        if (state_elapsed_ms >= E3_HEAT_SOAK_MS)
        {
            H1_Test_EnterState(STATE_STOP);
        }
        break;

    case STATE_STOP:
        if (state_elapsed_ms >= E3_STOP_MS)
        {
            H1_Test_EnterState(STATE_DONE);
        }
        break;

    case STATE_DONE:
    case STATE_ABORTED:
    default:
        break;
    }
}

const char *E3_Test_GetStateName(E3_TestState_t state)
{
    switch (state)
    {
    case STATE_ARMING:
        return "ARMING";
    case STATE_READY:
        return "READY";
    case STATE_PREPARE:
        return "PREPARE";
    case STATE_RAMP:
        return "RAMP";
    case STATE_STABILIZE:
        return "STABILIZE";
    case STATE_HEAT_SOAK:
        return "HEAT_SOAK";
    case STATE_STOP:
        return "STOP";
    case STATE_DONE:
        return "DONE";
    case STATE_ABORTED:
        return "ABORTED";
    default:
        return "UNKNOWN";
    }
}

void set_throttle_command(uint16_t cmd)
{
    g_h1.current_dshot_value = H1_ClampDshotThrottle(cmd);
    g_h1.current_cmd_us = g_h1.current_dshot_value;
}

void process_adc_average(E3_AdcProcessed_t *out)
{
    uint32_t i;
    uint32_t sum_i = 0U;
    uint32_t sum_vbat = 0U;
    float adc_to_volt = ADC_REF_VOLTAGE / ADC_MAX_COUNTS;

    if (out == NULL)
    {
        return;
    }

    for (i = 0U; i < H1_ADC_DMA_BUFFER_LENGTH; i += H1_ADC_CHANNEL_COUNT)
    {
        sum_i += g_adc_dma_buffer[i];
        sum_vbat += g_adc_dma_buffer[i + 1U];
    }

    out->adc_i_raw = (uint16_t)(sum_i / H1_ADC_DMA_SAMPLES_PER_CHANNEL);
    out->adc_vbat_raw = (uint16_t)(sum_vbat / H1_ADC_DMA_SAMPLES_PER_CHANNEL);

    out->v_i_sense = (float)out->adc_i_raw * adc_to_volt;
    out->v_vbat_adc = (float)out->adc_vbat_raw * adc_to_volt;
    out->vbat_V = out->v_vbat_adc * VBAT_DIVIDER_RATIO;
    H1_UpdateCurrentDerived(out);
}

void update_zero_offset(const E3_AdcProcessed_t *adc_data, uint32_t now_ms)
{
    uint32_t state_elapsed_ms;

    if (adc_data == NULL)
    {
        return;
    }

    if (g_h1.current_dshot_value != 0U)
    {
        return;
    }

    if ((now_ms - g_h1.last_zero_offset_sample_tick) < H1_ZERO_OFFSET_SAMPLE_INTERVAL_MS)
    {
        return;
    }

    g_h1.last_zero_offset_sample_tick = now_ms;

    if ((g_h1.state == STATE_ARMING) ||
        (g_h1.state == STATE_READY) ||
        (g_h1.state == STATE_PREPARE))
    {
        g_h1.zero_offset_sum_v += adc_data->v_i_sense;
        g_h1.zero_offset_sample_count++;

        if (g_h1.zero_offset_sample_count > 0U)
        {
            g_h1.baseline_zero_offset_voltage = g_h1.zero_offset_sum_v / (float)g_h1.zero_offset_sample_count;
            g_h1.active_zero_offset_voltage = g_h1.baseline_zero_offset_voltage;
        }
        return;
    }

    state_elapsed_ms = now_ms - g_h1.state_enter_tick;

    if (g_h1.state == STATE_STOP)
    {
        if (state_elapsed_ms >= H1_ZERO_TRACK_DELAY_MS)
        {
            g_h1.active_zero_offset_voltage +=
                H1_ZERO_TRACK_ALPHA_STOP * (adc_data->v_i_sense - g_h1.active_zero_offset_voltage);
        }
        return;
    }

    if ((g_h1.state == STATE_DONE) || (g_h1.state == STATE_ABORTED))
    {
        g_h1.active_zero_offset_voltage +=
            H1_ZERO_TRACK_ALPHA_DONE * (adc_data->v_i_sense - g_h1.active_zero_offset_voltage);
    }
}

static void H1_UpdateCurrentDerived(H1_AdcProcessed_t *adc_data)
{
    float signed_delta_v;

    if (adc_data == NULL)
    {
        return;
    }

    adc_data->active_zero_offset_V = g_h1.active_zero_offset_voltage;
    adc_data->delta_i_V = adc_data->v_i_sense - g_h1.active_zero_offset_voltage;

#if (H1_CURRENT_SIGN_INVERT != 0U)
    signed_delta_v = -adc_data->delta_i_V;
#else
    signed_delta_v = adc_data->delta_i_V;
#endif

    adc_data->current_A = signed_delta_v * CURRENT_SCALE_A_PER_V;
    adc_data->power_W = adc_data->current_A * adc_data->vbat_V;
}

static void H1_Test_EnterState(H1_TestState_t new_state)
{
    g_h1.state = new_state;
    g_h1.state_enter_tick = HAL_GetTick();

    switch (new_state)
    {
    case STATE_PREPARE:
    case STATE_READY:
    case STATE_DONE:
        g_h1.ramp_start_tick = 0U;
        g_h1.ramp_start_dshot = 0U;
        g_h1.ramp_target_dshot = 0U;
        set_throttle_command(0U);
        break;

    case STATE_STOP:
    case STATE_ABORTED:
        g_h1.ramp_start_tick = 0U;
        g_h1.ramp_start_dshot = 0U;
        g_h1.ramp_target_dshot = 0U;
        H1_ForceMotorStopFrame();
        break;

    case STATE_RAMP:
        g_h1.ramp_start_tick = g_h1.state_enter_tick;
        g_h1.ramp_start_dshot = H1_GetRampStartDshot();
        g_h1.ramp_target_dshot = g_h1.profile_dshot;
        set_throttle_command(g_h1.ramp_start_dshot);
        g_h1.last_dshot_send_tick = 0U;
        H1_Dshot_Service(g_h1.state_enter_tick);
        break;

    case STATE_STABILIZE:
        g_h1.ramp_start_tick = 0U;
        g_h1.ramp_start_dshot = 0U;
        g_h1.ramp_target_dshot = 0U;
        set_throttle_command(g_h1.profile_dshot);
        break;

    case STATE_HEAT_SOAK:
        g_h1.ramp_start_tick = 0U;
        g_h1.ramp_start_dshot = 0U;
        g_h1.ramp_target_dshot = 0U;
        g_h1.heat_soak_elapsed_ms = 0U;
        g_h1.next_checkpoint_index = 0U;
        H1_Trigger_Pulse(g_h1.state_enter_tick);
        set_throttle_command(g_h1.profile_dshot);
        break;

    default:
        set_throttle_command(0U);
        break;
    }
    H1_StatusLed_Update(g_h1.state_enter_tick);
    g_h1.last_oled_update_tick = 0U;
}

static void H1_StartSession(uint32_t now_ms)
{
    g_h1.test_start_tick = now_ms;
    g_h1.last_zero_offset_sample_tick = now_ms;
    g_h1.zero_offset_sum_v = 0.0f;
    g_h1.zero_offset_sample_count = 0U;
    g_h1.baseline_zero_offset_voltage = CURRENT_OFFSET_V;
    g_h1.active_zero_offset_voltage = CURRENT_OFFSET_V;
    g_h1.next_checkpoint_index = 0U;
    g_h1.heat_soak_elapsed_ms = 0U;
    g_h1.ramp_start_tick = 0U;
    g_h1.ramp_start_dshot = 0U;
    g_h1.ramp_target_dshot = 0U;

    H1_Test_EnterState(STATE_PREPARE);
    /* The start long-press must be released before it can become an abort press. */
    g_h1.abort_button_latched = 1U;
}

static void H1_Button_Service(uint32_t now_ms)
{
    AppButton_Event_t event = AppButton_Update(&g_h1.button, now_ms);
    uint8_t pressed = AppButton_IsPressed(&g_h1.button);

    if (g_h1.abort_button_latched != 0U)
    {
        if (pressed == 0U)
        {
            g_h1.abort_button_latched = 0U;
        }
        else if ((H1_IsAbortableState(g_h1.state) != 0U) &&
                 ((now_ms - g_h1.state_enter_tick) >= E3_BUTTON_LONG_PRESS_MS))
        {
            H1_RequestAbort();
        }
        return;
    }

    if ((pressed != 0U) && (H1_IsAbortableState(g_h1.state) != 0U))
    {
        g_h1.abort_button_latched = 1U;
        H1_RequestAbort();
        return;
    }

    if (event != APP_BUTTON_EVENT_NONE)
    {
        H1_HandleButtonEvent(event, now_ms);
    }
}

static void H1_HandleButtonEvent(AppButton_Event_t event, uint32_t now_ms)
{
    if (event == APP_BUTTON_EVENT_NONE)
    {
        return;
    }

    if (event == APP_BUTTON_EVENT_LONG_PRESS)
    {
        if ((g_h1.state == STATE_READY) ||
            (g_h1.state == STATE_DONE) ||
            (g_h1.state == STATE_ABORTED))
        {
            H1_StartSession(now_ms);
            return;
        }

        if (H1_IsAbortableState(g_h1.state) != 0U)
        {
            H1_RequestAbort();
        }
        return;
    }

    if ((g_h1.state == STATE_READY) ||
        (g_h1.state == STATE_DONE) ||
        (g_h1.state == STATE_ABORTED))
    {
        H1_SelectNextProfile();
        return;
    }

    if (H1_IsAbortableState(g_h1.state) != 0U)
    {
        H1_RequestAbort();
    }
}

static uint8_t H1_IsAbortableState(H1_TestState_t state)
{
    return ((state == STATE_PREPARE) ||
            (state == STATE_RAMP) ||
            (state == STATE_STABILIZE) ||
            (state == STATE_HEAT_SOAK) ||
            (state == STATE_STOP)) ? 1U : 0U;
}

static void H1_SelectNextProfile(void)
{
    uint8_t next_profile = (uint8_t)(g_h1.profile_index + 1U);

    if (next_profile >= E3_PROFILE_COUNT)
    {
        next_profile = 0U;
    }

    H1_LoadProfile(next_profile);
    g_h1.last_oled_update_tick = 0U;
}

static void H1_RequestAbort(void)
{
    H1_Test_EnterState(STATE_ABORTED);
}

static void H1_ForceMotorStopFrame(void)
{
    set_throttle_command(0U);
    H1_Trigger_Set(0U);
    g_h1.trigger_active = 0U;

    if (g_h1.dshot_dma_busy != 0U)
    {
        (void)HAL_TIM_PWM_Stop_DMA(&htim4, TIM_CHANNEL_3);
        g_h1.dshot_dma_busy = 0U;
    }

    __HAL_TIM_DISABLE(&htim4);
    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);
    g_h1.last_dshot_send_tick = 0U;
    H1_Dshot_TriggerFrame(0U);
}

static void H1_LoadProfile(uint8_t profile_index)
{
    const E3_Profile_t *profile;

    if (profile_index >= E3_PROFILE_COUNT)
    {
        profile_index = 0U;
    }

    profile = &g_e3_profiles[profile_index];
    g_h1.profile_index = profile_index;
    g_h1.profile_dshot = H1_ClampDshotThrottle(profile->dshot_cmd);
}

static uint16_t H1_GetRampStartDshot(void)
{
    return H1_ClampDshotThrottle(E3_RAMP_START_DSHOT);
}

static void H1_UpdateRampThrottle(uint32_t now_ms)
{
    uint32_t elapsed_ms;
    uint32_t ramp_elapsed_ms;
    uint32_t ramp_duration_ms;
    uint32_t delta;
    uint32_t ramped_cmd;

    if (g_h1.state != STATE_RAMP)
    {
        return;
    }

    if (g_h1.ramp_start_tick == 0U)
    {
        g_h1.ramp_start_tick = g_h1.state_enter_tick;
    }

    elapsed_ms = now_ms - g_h1.ramp_start_tick;

    if (elapsed_ms < E3_RAMP_START_HOLD_MS)
    {
        set_throttle_command(g_h1.ramp_start_dshot);
        return;
    }

    if (elapsed_ms >= E3_RAMP_MS)
    {
        set_throttle_command(g_h1.ramp_target_dshot);
        return;
    }

    if (E3_RAMP_MS <= E3_RAMP_START_HOLD_MS)
    {
        set_throttle_command(g_h1.ramp_target_dshot);
        return;
    }

    ramp_elapsed_ms = elapsed_ms - E3_RAMP_START_HOLD_MS;
    ramp_duration_ms = E3_RAMP_MS - E3_RAMP_START_HOLD_MS;

    if (g_h1.ramp_target_dshot >= g_h1.ramp_start_dshot)
    {
        delta = (uint32_t)g_h1.ramp_target_dshot - (uint32_t)g_h1.ramp_start_dshot;
        ramped_cmd = (uint32_t)g_h1.ramp_start_dshot + ((delta * ramp_elapsed_ms) / ramp_duration_ms);
    }
    else
    {
        delta = (uint32_t)g_h1.ramp_start_dshot - (uint32_t)g_h1.ramp_target_dshot;
        ramped_cmd = (uint32_t)g_h1.ramp_start_dshot - ((delta * ramp_elapsed_ms) / ramp_duration_ms);
    }

    set_throttle_command((uint16_t)ramped_cmd);
}

static void H1_HeatSoak_Service(uint32_t now_ms, uint32_t state_elapsed_ms)
{
    g_h1.heat_soak_elapsed_ms = state_elapsed_ms;

    while ((g_h1.next_checkpoint_index < E3_CHECKPOINT_COUNT) &&
           (state_elapsed_ms >= g_e3_checkpoint_ms[g_h1.next_checkpoint_index]))
    {
        H1_Trigger_Pulse(now_ms);
        g_h1.next_checkpoint_index++;
    }
}

static int32_t H1_GetActiveCheckpointMs(uint32_t run_elapsed_ms)
{
    uint8_t i;

    if (run_elapsed_ms <= E3_REMINDER_WINDOW_MS)
    {
        return 0;
    }

    for (i = 0U; i < E3_CHECKPOINT_COUNT; i++)
    {
        if ((run_elapsed_ms >= (g_e3_checkpoint_ms[i] - E3_REMINDER_WINDOW_MS)) &&
            (run_elapsed_ms <= g_e3_checkpoint_ms[i]))
        {
            return (int32_t)g_e3_checkpoint_ms[i];
        }
    }

    return -1;
}

static void H1_FormatFloat3(char *dst, size_t len, float value)
{
    int32_t scaled;
    int32_t abs_scaled;
    int32_t integer_part;
    int32_t fractional_part;
    const char *sign_str;

    if ((dst == NULL) || (len == 0U))
    {
        return;
    }

    scaled = (int32_t)(value * 1000.0f + ((value >= 0.0f) ? 0.5f : -0.5f));
    sign_str = (scaled < 0) ? "-" : "";
    abs_scaled = (scaled < 0) ? -scaled : scaled;
    integer_part = abs_scaled / 1000;
    fractional_part = abs_scaled % 1000;

    (void)snprintf(dst, len, "%s%ld.%03ld", sign_str, (long)integer_part, (long)fractional_part);
}

static HAL_StatusTypeDef H1_Oled_WriteCommand(uint8_t command)
{
    uint8_t frame[2];

    if (g_h1.oled_ready == 0U)
    {
        return HAL_ERROR;
    }

    frame[0] = 0x00U;
    frame[1] = command;
    return HAL_I2C_Master_Transmit(&hi2c1, H1_OLED_I2C_ADDR, frame, 2U, H1_OLED_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef H1_Oled_WriteData(const uint8_t *data, uint16_t size)
{
    uint8_t frame[129];

    if ((g_h1.oled_ready == 0U) || (data == NULL) || (size > 128U))
    {
        return HAL_ERROR;
    }

    frame[0] = 0x40U;
    memcpy(&frame[1], data, size);
    return HAL_I2C_Master_Transmit(&hi2c1, H1_OLED_I2C_ADDR, frame, (uint16_t)(size + 1U), H1_OLED_I2C_TIMEOUT_MS);
}

static void H1_Oled_Init(void)
{
    static const uint8_t init_cmds[] =
    {
        0xAEU, 0x20U, 0x02U, 0xB0U, 0xC8U, 0x00U, 0x10U, 0x40U,
        0x81U, 0x7FU, 0xA1U, 0xA6U, 0xA8U, 0x3FU, 0xA4U, 0xD3U,
        0x00U, 0xD5U, 0x80U, 0xD9U, 0xF1U, 0xDAU, 0x12U, 0xDBU,
        0x20U, 0x8DU, 0x14U, 0xAFU
    };
    uint8_t i;

    g_h1.oled_ready = 0U;

    if (HAL_I2C_IsDeviceReady(&hi2c1, H1_OLED_I2C_ADDR, 2U, 100U) != HAL_OK)
    {
        return;
    }

    g_h1.oled_ready = 1U;

    for (i = 0U; i < (uint8_t)(sizeof(init_cmds) / sizeof(init_cmds[0])); i++)
    {
        if (H1_Oled_WriteCommand(init_cmds[i]) != HAL_OK)
        {
            g_h1.oled_ready = 0U;
            return;
        }
    }

    H1_Oled_Clear();
}

static void H1_Oled_Service(uint32_t now_ms)
{
    if (g_h1.oled_ready != 0U)
    {
        return;
    }

    if (g_h1.last_oled_init_attempt_tick == 0U)
    {
        if (now_ms < H1_OLED_POWERUP_DELAY_MS)
        {
            return;
        }
    }
    else if ((now_ms - g_h1.last_oled_init_attempt_tick) < H1_OLED_RETRY_INTERVAL_MS)
    {
        return;
    }

    g_h1.last_oled_init_attempt_tick = now_ms;
    H1_Oled_Init();

    if (g_h1.oled_ready != 0U)
    {
        g_h1.last_oled_update_tick = 0U;
        H1_Oled_Update(now_ms);
    }
}

static void H1_Oled_Clear(void)
{
    if (g_h1.oled_ready == 0U)
    {
        return;
    }

    memset(g_oled_buffer, 0, sizeof(g_oled_buffer));
    H1_Oled_Flush();
}

static void H1_Oled_Flush(void)
{
    uint8_t page;

    if (g_h1.oled_ready == 0U)
    {
        return;
    }

    for (page = 0U; page < 8U; page++)
    {
        if ((H1_Oled_WriteCommand((uint8_t)(0xB0U | page)) != HAL_OK) ||
            (H1_Oled_WriteCommand((uint8_t)(0x00U | (H1_OLED_COLUMN_OFFSET & 0x0FU))) != HAL_OK) ||
            (H1_Oled_WriteCommand((uint8_t)(0x10U | ((H1_OLED_COLUMN_OFFSET >> 4U) & 0x0FU))) != HAL_OK) ||
            (H1_Oled_WriteData(&g_oled_buffer[page * 128U], 128U) != HAL_OK))
        {
            g_h1.oled_ready = 0U;
            g_h1.last_oled_init_attempt_tick = HAL_GetTick();
            return;
        }
    }
}

static void H1_Oled_Update(uint32_t now_ms)
{
    char line0[32];
    char line1[32];
    char line2[32];
    char line3[32];
    char line4[32];
    char value_str[20];
    uint32_t state_elapsed_ms;
    uint32_t remain_s;
    int32_t reminder_ms;
    const E3_Profile_t *profile;

    if (g_h1.oled_ready == 0U)
    {
        return;
    }

    if ((g_h1.last_oled_update_tick != 0U) &&
        ((now_ms - g_h1.last_oled_update_tick) < H1_OLED_UPDATE_INTERVAL_MS))
    {
        return;
    }

    g_h1.last_oled_update_tick = now_ms;
    memset(g_oled_buffer, 0, sizeof(g_oled_buffer));

    H1_FormatFloat3(value_str, sizeof(value_str), g_h1.adc.vbat_V);
    (void)snprintf(line0, sizeof(line0), "VBAT %sV", value_str);

    H1_FormatFloat3(value_str, sizeof(value_str), g_h1.adc.current_A);
    (void)snprintf(line1, sizeof(line1), "CURR %sA", value_str);

    H1_FormatFloat3(value_str, sizeof(value_str), g_h1.adc.power_W);
    (void)snprintf(line2, sizeof(line2), "POWR %sW", value_str);

    profile = &g_e3_profiles[g_h1.profile_index];
    state_elapsed_ms = now_ms - g_h1.state_enter_tick;

    (void)snprintf(line3,
                   sizeof(line3),
                   "P%u %s%s CMD %u",
                   (unsigned int)(g_h1.profile_index + 1U),
                   profile->oled_name,
                   profile->rpm_point,
                   (unsigned int)g_h1.profile_dshot);

    switch (g_h1.state)
    {
    case STATE_ARMING:
        remain_s = (state_elapsed_ms >= E3_ESC_ARMING_MS) ? 0U : (E3_ESC_ARMING_MS - state_elapsed_ms + 999U) / 1000U;
        (void)snprintf(line4, sizeof(line4), "ARM %2luS", (unsigned long)remain_s);
        break;

    case STATE_READY:
        (void)snprintf(line4, sizeof(line4), "READY");
        break;

    case STATE_PREPARE:
        remain_s = (state_elapsed_ms >= E3_PREPARE_MS) ? 0U : (E3_PREPARE_MS - state_elapsed_ms + 999U) / 1000U;
        (void)snprintf(line4, sizeof(line4), "PREP %2luS", (unsigned long)remain_s);
        break;

    case STATE_RAMP:
        (void)snprintf(line4,
                       sizeof(line4),
                       "RMP %04u",
                       (unsigned int)g_h1.current_dshot_value);
        break;

    case STATE_STABILIZE:
        remain_s = (state_elapsed_ms >= E3_STABILIZE_MS) ? 0U : (E3_STABILIZE_MS - state_elapsed_ms + 999U) / 1000U;
        (void)snprintf(line4, sizeof(line4), "STAB %2luS", (unsigned long)remain_s);
        break;

    case STATE_HEAT_SOAK:
        reminder_ms = H1_GetActiveCheckpointMs(state_elapsed_ms);
        if (reminder_ms >= 0)
        {
            (void)snprintf(line4,
                           sizeof(line4),
                           "IMG %03luS",
                           (unsigned long)((uint32_t)reminder_ms / 1000U));
        }
        else
        {
            (void)snprintf(line4,
                           sizeof(line4),
                           "RUN %03luS",
                           (unsigned long)(state_elapsed_ms / 1000U));
        }
        break;

    case STATE_STOP:
        remain_s = (state_elapsed_ms >= E3_STOP_MS) ? 0U : (E3_STOP_MS - state_elapsed_ms + 999U) / 1000U;
        (void)snprintf(line4, sizeof(line4), "STOP %2luS", (unsigned long)remain_s);
        break;

    case STATE_DONE:
        (void)snprintf(line4, sizeof(line4), "DONE");
        break;

    case STATE_ABORTED:
    default:
        (void)snprintf(line4, sizeof(line4), "ABORT");
        break;
    }

    H1_Oled_DrawLine(0U, line0);
    H1_Oled_DrawLine(1U, line1);
    H1_Oled_DrawLine(2U, line2);
    H1_Oled_DrawLine(3U, line3);
    H1_Oled_DrawLine(4U, line4);
    H1_Oled_Flush();
}

static void H1_Oled_DrawLine(uint8_t line_index, const char *text)
{
    uint8_t x = 0U;
    uint8_t y;

    if ((g_h1.oled_ready == 0U) || (line_index >= 5U))
    {
        return;
    }

    y = (uint8_t)(line_index * 11U);

    while ((text != NULL) && (*text != '\0') && (x <= 120U))
    {
        H1_Oled_DrawChar(x, y, *text);
        x = (uint8_t)(x + 7U);
        text++;
    }
}

static void H1_Oled_DrawChar(uint8_t x, uint8_t y, char c)
{
    uint8_t glyph[5];
    uint8_t col;
    uint8_t row;

    H1_Oled_GetGlyph5x7(c, glyph);

    for (col = 0U; col < 5U; col++)
    {
        for (row = 0U; row < 7U; row++)
        {
            if ((glyph[col] & (uint8_t)(1U << row)) != 0U)
            {
                H1_Oled_SetPixel((uint8_t)(x + col), (uint8_t)(y + row), 1U);
                H1_Oled_SetPixel((uint8_t)(x + col + 1U), (uint8_t)(y + row), 1U);
            }
        }
    }
}

static void H1_Oled_SetPixel(uint8_t x, uint8_t y, uint8_t on)
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

static void H1_Oled_GetGlyph5x7(char c, uint8_t glyph[5])
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
    case 'G': glyph[0] = 0x3EU; glyph[1] = 0x41U; glyph[2] = 0x49U; glyph[3] = 0x49U; glyph[4] = 0x7AU; break;
    case 'H': glyph[0] = 0x7FU; glyph[1] = 0x08U; glyph[2] = 0x08U; glyph[3] = 0x08U; glyph[4] = 0x7FU; break;
    case 'I': glyph[0] = 0x00U; glyph[1] = 0x41U; glyph[2] = 0x7FU; glyph[3] = 0x41U; glyph[4] = 0x00U; break;
    case 'L': glyph[0] = 0x7FU; glyph[1] = 0x40U; glyph[2] = 0x40U; glyph[3] = 0x40U; glyph[4] = 0x40U; break;
    case 'M': glyph[0] = 0x7FU; glyph[1] = 0x02U; glyph[2] = 0x0CU; glyph[3] = 0x02U; glyph[4] = 0x7FU; break;
    case 'N': glyph[0] = 0x7FU; glyph[1] = 0x04U; glyph[2] = 0x08U; glyph[3] = 0x10U; glyph[4] = 0x7FU; break;
    case 'O': glyph[0] = 0x3EU; glyph[1] = 0x41U; glyph[2] = 0x41U; glyph[3] = 0x41U; glyph[4] = 0x3EU; break;
    case 'P': glyph[0] = 0x7FU; glyph[1] = 0x09U; glyph[2] = 0x09U; glyph[3] = 0x09U; glyph[4] = 0x06U; break;
    case 'R': glyph[0] = 0x7FU; glyph[1] = 0x09U; glyph[2] = 0x19U; glyph[3] = 0x29U; glyph[4] = 0x46U; break;
    case 'S': glyph[0] = 0x46U; glyph[1] = 0x49U; glyph[2] = 0x49U; glyph[3] = 0x49U; glyph[4] = 0x31U; break;
    case 'T': glyph[0] = 0x01U; glyph[1] = 0x01U; glyph[2] = 0x7FU; glyph[3] = 0x01U; glyph[4] = 0x01U; break;
    case 'U': glyph[0] = 0x3FU; glyph[1] = 0x40U; glyph[2] = 0x40U; glyph[3] = 0x40U; glyph[4] = 0x3FU; break;
    case 'V': glyph[0] = 0x1FU; glyph[1] = 0x20U; glyph[2] = 0x40U; glyph[3] = 0x20U; glyph[4] = 0x1FU; break;
    case 'W': glyph[0] = 0x7FU; glyph[1] = 0x20U; glyph[2] = 0x18U; glyph[3] = 0x20U; glyph[4] = 0x7FU; break;
    case 'Y': glyph[0] = 0x03U; glyph[1] = 0x04U; glyph[2] = 0x78U; glyph[3] = 0x04U; glyph[4] = 0x03U; break;
    default: break;
    }
}

static void H1_StatusLed_Set(uint8_t on)
{
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port,
                      STATUS_LED_Pin,
                      (on != 0U) ? H1_STATUS_LED_ON_LEVEL : H1_STATUS_LED_OFF_LEVEL);
}

static void H1_StatusLed_Update(uint32_t now_ms)
{
    uint32_t phase_ms;

    switch (g_h1.state)
    {
    case STATE_ARMING:
        phase_ms = now_ms % 500U;
        H1_StatusLed_Set((phase_ms < 80U) ? 1U : 0U);
        break;

    case STATE_READY:
        phase_ms = now_ms % 2000U;
        H1_StatusLed_Set(((phase_ms < 80U) || ((phase_ms >= 250U) && (phase_ms < 330U))) ? 1U : 0U);
        break;

    case STATE_PREPARE:
        phase_ms = now_ms % 1000U;
        H1_StatusLed_Set((phase_ms < 120U) ? 1U : 0U);
        break;

    case STATE_RAMP:
        phase_ms = now_ms % 300U;
        H1_StatusLed_Set((phase_ms < 150U) ? 1U : 0U);
        break;

    case STATE_STABILIZE:
        phase_ms = now_ms % 1000U;
        H1_StatusLed_Set((phase_ms < 500U) ? 1U : 0U);
        break;

    case STATE_HEAT_SOAK:
        H1_StatusLed_Set(1U);
        break;

    case STATE_STOP:
        phase_ms = now_ms % 250U;
        H1_StatusLed_Set((phase_ms < 125U) ? 1U : 0U);
        break;

    case STATE_DONE:
        H1_StatusLed_Set(0U);
        break;

    case STATE_ABORTED:
        phase_ms = now_ms % 500U;
        H1_StatusLed_Set((phase_ms < 250U) ? 1U : 0U);
        break;

    default:
        H1_StatusLed_Set(0U);
        break;
    }
}

static void H1_Trigger_Set(uint8_t high)
{
    HAL_GPIO_WritePin(E3_TRIGGER_GPIO_Port,
                      E3_TRIGGER_Pin,
                      (high != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void H1_Trigger_Pulse(uint32_t now_ms)
{
    g_h1.trigger_active = 1U;
    g_h1.trigger_release_tick = now_ms + E3_TRIGGER_PULSE_MS;
    H1_Trigger_Set(1U);
}

static void H1_Trigger_Service(uint32_t now_ms)
{
    if ((g_h1.trigger_active != 0U) && ((int32_t)(now_ms - g_h1.trigger_release_tick) >= 0))
    {
        g_h1.trigger_active = 0U;
        H1_Trigger_Set(0U);
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
    g_h1.dshot_dma_busy = 0U;
}

static uint16_t H1_ClampDshotThrottle(uint16_t cmd)
{
    if (cmd == 0U)
    {
        return 0U;
    }

    if (cmd < H1_DSHOT_MIN_THROTTLE)
    {
        return H1_DSHOT_MIN_THROTTLE;
    }

    if (cmd > H1_DSHOT_MAX_THROTTLE)
    {
        return H1_DSHOT_MAX_THROTTLE;
    }

    return cmd;
}

static uint16_t H1_Dshot_BuildPacket(uint16_t throttle_value)
{
    uint16_t packet;
    uint16_t checksum;
    uint16_t csum_data;
    uint8_t i;

    packet = (uint16_t)((throttle_value << 1U) | (H1_DSHOT_TELEMETRY_BIT & 0x1U));
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

static void H1_Dshot_PrepareFrame(uint16_t throttle_value)
{
    uint16_t packet;
    uint8_t i;

    packet = H1_Dshot_BuildPacket(throttle_value);

    for (i = 0U; i < H1_DSHOT_FRAME_BITS; i++)
    {
        g_dshot_dma_buffer[i] = ((packet & 0x8000U) != 0U) ? H1_DSHOT_BIT_1_HIGH_TICKS : H1_DSHOT_BIT_0_HIGH_TICKS;
        packet <<= 1U;
    }

    for (i = H1_DSHOT_FRAME_BITS; i < H1_DSHOT_DMA_BUFFER_LENGTH; i++)
    {
        g_dshot_dma_buffer[i] = 0U;
    }
}

static void H1_Dshot_TriggerFrame(uint16_t throttle_value)
{
    HAL_StatusTypeDef status;

    if (g_h1.dshot_dma_busy != 0U)
    {
        return;
    }

    H1_Dshot_PrepareFrame(throttle_value);
    g_h1.dshot_dma_busy = 1U;

    __HAL_TIM_DISABLE(&htim4);
    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, g_dshot_dma_buffer[0]);

    status = HAL_TIM_PWM_Start_DMA(&htim4,
                                   TIM_CHANNEL_3,
                                   (const uint32_t *)&g_dshot_dma_buffer[1],
                                   (uint16_t)(H1_DSHOT_DMA_BUFFER_LENGTH - 1U));
    if (status != HAL_OK)
    {
        __HAL_TIM_DISABLE(&htim4);
        __HAL_TIM_SET_COUNTER(&htim4, 0U);
        __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);
        g_h1.dshot_dma_busy = 0U;
        set_throttle_command(0U);
        g_h1.state = STATE_ABORTED;
        return;
    }
}

static void H1_Dshot_Service(uint32_t now_ms)
{
    if ((now_ms - g_h1.last_dshot_send_tick) < H1_DSHOT_SEND_INTERVAL_MS)
    {
        return;
    }

    if (g_h1.dshot_dma_busy != 0U)
    {
        return;
    }

    g_h1.last_dshot_send_tick = now_ms;
    H1_Dshot_TriggerFrame(g_h1.current_dshot_value);
}

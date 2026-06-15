#include "app_p0_rpm_map.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "adc.h"
#include "gpio.h"
#include "i2c.h"
#include "tim.h"
#include "usart.h"

#define P0_APP_VERSION                    "0.1"
#define P0_ADC_CHANNEL_COUNT              2U
#define P0_ADC_DMA_SAMPLES_PER_CHANNEL    64U
#define P0_ADC_DMA_BUFFER_LENGTH          (P0_ADC_CHANNEL_COUNT * P0_ADC_DMA_SAMPLES_PER_CHANNEL)

#define P0_DSHOT_FRAME_BITS               16U
#define P0_DSHOT_RESET_SLOTS              8U
#define P0_DSHOT_DMA_BUFFER_LENGTH        (P0_DSHOT_FRAME_BITS + P0_DSHOT_RESET_SLOTS)
#define P0_DSHOT_BIT_0_HIGH_TICKS         90U
#define P0_DSHOT_BIT_1_HIGH_TICKS         180U

#define P0_STATUS_LED_ON_LEVEL            GPIO_PIN_RESET
#define P0_STATUS_LED_OFF_LEVEL           GPIO_PIN_SET
#define P0_BT_CONNECTED_LEVEL             GPIO_PIN_SET
#define P0_MEASURE_SAMPLE_INTERVAL_MS     20U

typedef enum
{
    P0_BOARD_UNKNOWN = 0,
    P0_BOARD_SI,
    P0_BOARD_GAN
} P0_Board_t;

typedef enum
{
    P0_LOAD_UNKNOWN = 0,
    P0_LOAD_PROP,
    P0_LOAD_NOPROP
} P0_Load_t;

typedef struct
{
    float sum_vbat;
    float sum_current;
    float sum_power;
    float sum_vbat2;
    float sum_current2;
    float sum_power2;
    uint32_t count;
} P0_MeasureStats_t;

typedef struct
{
    p0_state_t state;
    uint32_t boot_tick;
    uint32_t session_start_tick;
    uint32_t state_enter_tick;
    uint32_t last_csv_tick;
    uint32_t last_dshot_send_tick;
    uint32_t last_zero_track_tick;
    uint32_t last_measure_sample_tick;
    uint32_t last_oled_update_tick;
    uint32_t last_oled_init_attempt_tick;
    uint32_t button_change_tick;
    uint32_t button_press_tick;
    uint32_t bt_high_since_tick;
    uint32_t bt_low_since_tick;
    uint32_t current_trip_start_tick;
    uint32_t ramp_duration_ms;
    uint16_t current_dshot_value;
    uint16_t target_dshot_value;
    uint16_t ramp_start_dshot_value;
    uint16_t preview_dshot_value;
    uint16_t sweep[P0_MAX_SWEEP_POINTS];
    uint8_t sweep_count;
    uint8_t step_index;
    uint8_t rpm_count;
    uint32_t rpm_readings[P0_RPM_MAX_READINGS];
    P0_MeasureStats_t stats;
    float baseline_zero_offset_voltage;
    float active_zero_offset_voltage;
    P0_AdcProcessed_t adc;
    P0_Board_t board;
    P0_Load_t load;
    uint8_t boot_banner_sent;
    uint8_t connected_banner_sent;
    uint8_t ready_banner_sent;
    uint8_t csv_header_sent;
    uint8_t dshot_dma_busy;
    uint8_t bt_state_last;
    uint8_t bt_confirmed;
    uint8_t oled_ready;
    uint8_t button_raw_pressed;
    uint8_t button_stable_pressed;
    uint8_t button_long_handled;
    uint8_t summary_sent;
    uint8_t repeat_requested;
    uint8_t fault_latched;
    const char *fault_reason;
} P0_Context_t;

static P0_Context_t g_p0;
static volatile uint16_t g_adc_dma_buffer[P0_ADC_DMA_BUFFER_LENGTH];
static uint16_t g_dshot_dma_buffer[P0_DSHOT_DMA_BUFFER_LENGTH];
static uint8_t g_oled_buffer[128U * 8U];

static uint8_t g_uart_rx_byte;
static char g_uart_cmd_buffer[P0_UART_CMD_BUFFER_SIZE];
static uint8_t g_uart_cmd_index;
static char g_uart_pending_cmd[P0_UART_CMD_BUFFER_SIZE];
static volatile uint8_t g_uart_cmd_pending;
static uint8_t g_uart_tx_buffer[P0_UART_TX_BUFFER_SIZE];
static volatile uint16_t g_uart_tx_head;
static volatile uint16_t g_uart_tx_tail;
static volatile uint8_t g_uart_tx_busy;
static volatile uint8_t g_uart_rx_active;
static uint8_t g_uart_tx_byte;

static void P0_LoadDefaultSweep(uint8_t extended);
static uint8_t P0_SetSweep(uint16_t start, uint16_t end, uint16_t step);
static void P0_StartMapping(uint32_t now_ms);
static void P0_BeginCurrentStep(void);
#if (P0_REQUIRE_NEXT_TO_ADVANCE == 0U)
static void P0_FinishCurrentPoint(void);
#endif
static void P0_SendCurrentStepSummaryOnce(const char *status);
#if (P0_REQUIRE_NEXT_TO_ADVANCE != 0U)
static void P0_AdvanceToNextStepWithoutStop(void);
#endif
static void P0_AdvanceAfterRest(void);
static void P0_EnterState(p0_state_t state);
static uint32_t P0_ElapsedMs(uint32_t now_ms, uint32_t since_ms);
static uint8_t P0_IsBluetoothConnected(void);
static uint8_t P0_IsStoppedState(void);
static uint8_t P0_IsActiveState(void);
static void P0_ProcessAdcAverage(P0_AdcProcessed_t *out);
static void P0_UpdateZeroOffset(const P0_AdcProcessed_t *adc_data, uint32_t now_ms);
static void P0_UpdateCurrentDerived(P0_AdcProcessed_t *adc_data);
static void P0_MeasureStats_Reset(void);
static void P0_MeasureStats_Accumulate(uint32_t now_ms);
static float P0_StatsMean(float sum, uint32_t count);
static float P0_StatsStd(float sum, float sum2, uint32_t count);
static void P0_SafetyTask(uint32_t now_ms);
static void P0_TriggerFault(const char *reason);
static void P0_Abort(const char *reason);
static void P0_ButtonTask(uint32_t now_ms);
static void P0_CyclePreviewCommand(void);
static void P0_StatusLed_Set(uint8_t on);
static void P0_StatusLed_Update(uint32_t now_ms);
static uint16_t P0_ClampDshotThrottle(uint16_t cmd);
static uint16_t P0_Dshot_BuildPacket(uint16_t throttle_value);
static void P0_Dshot_PrepareFrame(uint16_t throttle_value);
static void P0_Dshot_TriggerFrame(uint16_t throttle_value);
static void P0_Dshot_Service(uint32_t now_ms);
static void P0_ForceMotorStopFrame(void);
static void P0_UpdateRampCommand(uint32_t now_ms);
static void P0_UartRx_Start(void);
static void P0_ProcessReceivedByte(uint8_t byte);
static void P0_ProcessPendingCommand(void);
static void P0_HandleCommand(const char *cmd);
static uint8_t P0_UartWrite(const char *text);
static uint8_t P0_UartCanSend(void);
static void P0_UartTx_StartNext(void);
static void P0_UartTx_ResetQueue(void);
static char P0_ToUpperChar(char c);
static void P0_ToUpperString(char *dst, size_t dst_len, const char *src);
static char *P0_NextToken(char **cursor);
static uint8_t P0_ParseU16(const char *text, uint16_t *out);
#if (P0_ENABLE_UART_RPM_INPUT != 0U)
static uint8_t P0_ParseU32(const char *text, uint32_t *out);
#endif
static void P0_SendBootBannerOnce(void);
static void P0_SendConnectedBannerOnce(void);
static void P0_SendReadyBannerOnce(void);
static void P0_SendHelp(void);
static void P0_SendStatus(void);
static void P0_SendCsvHeaderOnce(void);
static void P0_SendCsvLine(const P0_AdcProcessed_t *adc_data);
static void P0_SendSweepSetLine(void);
#if (P0_ENABLE_UART_RPM_INPUT != 0U)
static void P0_SendRpmAddedLine(uint32_t rpm);
#endif
static void P0_SendStepSummary(const char *status);
static const char *P0_CurrentStepSummaryStatus(void);
static const char *P0_BoardName(void);
static const char *P0_LoadName(void);
static uint32_t P0_RpmMedian(void);
static float P0_RpmMean(void);
static uint32_t P0_RpmMin(void);
static uint32_t P0_RpmMax(void);
static void P0_ClearRpmReadings(void);
static void P0_FormatFloat(char *dst, size_t len, float value, uint8_t decimals);
static void P0_FormatFloat3(char *dst, size_t len, float value);
static void P0_FormatFloat1(char *dst, size_t len, float value);
static HAL_StatusTypeDef P0_Oled_WriteCommand(uint8_t command);
static HAL_StatusTypeDef P0_Oled_WriteData(const uint8_t *data, uint16_t size);
static void P0_Oled_Init(void);
static void P0_Oled_Service(uint32_t now_ms);
static void P0_Oled_Clear(void);
static void P0_Oled_Flush(void);
static void P0_Oled_Update(uint32_t now_ms);
static void P0_Oled_DrawLine(uint8_t page, const char *text);
static void P0_Oled_DrawChar(uint8_t x, uint8_t y, char c);
static void P0_Oled_SetPixel(uint8_t x, uint8_t y, uint8_t on);
static void P0_Oled_GetGlyph5x7(char c, uint8_t glyph[5]);

void P0_RpmMap_Init(void)
{
    memset(&g_p0, 0, sizeof(g_p0));

    g_p0.boot_tick = HAL_GetTick();
    g_p0.session_start_tick = g_p0.boot_tick;
    g_p0.state_enter_tick = g_p0.boot_tick;
    g_p0.state = (P0_REQUIRE_BT != 0U) ? P0_STATE_WAIT_BT : P0_STATE_READY;
    g_p0.baseline_zero_offset_voltage = CURRENT_OFFSET_V;
    g_p0.active_zero_offset_voltage = CURRENT_OFFSET_V;
    g_p0.preview_dshot_value = P0_DEFAULT_SWEEP_START;
    g_p0.ramp_duration_ms = P0_RAMP_MS;
    g_p0.load = P0_LOAD_PROP;
    g_p0.bt_state_last = P0_IsBluetoothConnected();
    P0_LoadDefaultSweep(0U);

    P0_ForceMotorStopFrame();
    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);

    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_dma_buffer, P0_ADC_DMA_BUFFER_LENGTH) != HAL_OK)
    {
        Error_Handler();
    }

    P0_UartRx_Start();
    HAL_Delay(P0_OLED_POWERUP_DELAY_MS);
    P0_Oled_Service(HAL_GetTick());
    P0_Dshot_TriggerFrame(0U);
    P0_StatusLed_Update(HAL_GetTick());
}

void P0_RpmMap_Task(void)
{
    uint32_t now_ms = HAL_GetTick();
    uint32_t elapsed_ms;
    uint8_t bt_connected;

    P0_ProcessAdcAverage(&g_p0.adc);
    P0_UpdateZeroOffset(&g_p0.adc, now_ms);
    P0_UpdateCurrentDerived(&g_p0.adc);
    P0_ProcessPendingCommand();
    P0_SafetyTask(now_ms);
    P0_UartRx_Start();
    P0_UpdateRampCommand(now_ms);
    P0_Dshot_Service(now_ms);
    P0_StatusLed_Update(now_ms);
    P0_ButtonTask(now_ms);
    P0_Oled_Service(now_ms);
    P0_Oled_Update(now_ms);
    P0_MeasureStats_Accumulate(now_ms);

    bt_connected = P0_IsBluetoothConnected();
    if ((P0_REQUIRE_BT != 0U) &&
        ((g_p0.state == P0_STATE_PREPARE) ||
         (g_p0.state == P0_STATE_RAMP) ||
         (g_p0.state == P0_STATE_STABILIZE) ||
         (g_p0.state == P0_STATE_MEASURE) ||
         (g_p0.state == P0_STATE_REST)))
    {
        if (bt_connected == 0U)
        {
            if (g_p0.bt_low_since_tick == 0U)
            {
                g_p0.bt_low_since_tick = now_ms;
            }
            else if (P0_ElapsedMs(now_ms, g_p0.bt_low_since_tick) >= P0_BT_LOSS_ABORT_MS)
            {
                P0_Abort("bt_lost");
            }
        }
        else
        {
            g_p0.bt_low_since_tick = 0U;
        }
    }
    else if ((P0_REQUIRE_BT != 0U) &&
             (bt_connected == 0U) &&
             ((g_p0.state == P0_STATE_READY) ||
              (g_p0.state == P0_STATE_DONE) ||
              (g_p0.state == P0_STATE_ABORTED)))
    {
        g_p0.bt_low_since_tick = 0U;
        g_p0.connected_banner_sent = 0U;
        g_p0.ready_banner_sent = 0U;
        P0_EnterState(P0_STATE_WAIT_BT);
    }

    if (g_p0.state == P0_STATE_WAIT_BT)
    {
        P0_ForceMotorStopFrame();
        if (bt_connected != 0U)
        {
            if (g_p0.bt_state_last == 0U)
            {
                g_p0.bt_high_since_tick = now_ms;
            }

            if (P0_ElapsedMs(now_ms, g_p0.bt_high_since_tick) >= P0_BT_CONNECT_CONFIRM_MS)
            {
                g_p0.bt_confirmed = 1U;
                P0_SendBootBannerOnce();
                P0_SendConnectedBannerOnce();
                P0_EnterState(P0_STATE_READY);
                P0_SendReadyBannerOnce();
            }
        }
        else
        {
            g_p0.bt_high_since_tick = 0U;
            g_p0.bt_low_since_tick = 0U;
            g_p0.bt_confirmed = 0U;
            g_p0.boot_banner_sent = 0U;
            g_p0.connected_banner_sent = 0U;
            g_p0.ready_banner_sent = 0U;
            P0_UartTx_ResetQueue();
        }

        g_p0.bt_state_last = bt_connected;
        return;
    }

    g_p0.bt_state_last = bt_connected;
    P0_SendBootBannerOnce();
    if ((P0_REQUIRE_BT == 0U) || (bt_connected != 0U))
    {
        P0_SendConnectedBannerOnce();
        P0_SendReadyBannerOnce();
    }

    if (P0_ElapsedMs(now_ms, g_p0.last_csv_tick) >= P0_CSV_INTERVAL_MS)
    {
        g_p0.last_csv_tick = now_ms;
        P0_SendCsvLine(&g_p0.adc);
    }

    elapsed_ms = P0_ElapsedMs(now_ms, g_p0.state_enter_tick);
    switch (g_p0.state)
    {
    case P0_STATE_READY:
        P0_ForceMotorStopFrame();
        break;

    case P0_STATE_PREPARE:
        if (elapsed_ms >= P0_PREPARE_MS)
        {
            P0_BeginCurrentStep();
        }
        break;

    case P0_STATE_RAMP:
        if (elapsed_ms >= g_p0.ramp_duration_ms)
        {
            P0_EnterState(P0_STATE_STABILIZE);
        }
        break;

    case P0_STATE_STABILIZE:
        if (elapsed_ms >= P0_STABILIZE_MS)
        {
            P0_EnterState(P0_STATE_MEASURE);
        }
        break;

    case P0_STATE_MEASURE:
#if (P0_REQUIRE_NEXT_TO_ADVANCE == 0U)
        if (elapsed_ms >= P0_MEASURE_MS)
        {
            P0_FinishCurrentPoint();
        }
#endif
        break;

    case P0_STATE_REST:
        P0_ForceMotorStopFrame();
        if (elapsed_ms >= P0_REST_MS)
        {
            P0_AdvanceAfterRest();
        }
        break;

    case P0_STATE_DONE:
    case P0_STATE_ABORTED:
    case P0_STATE_FAULT:
        P0_ForceMotorStopFrame();
        break;

    case P0_STATE_WAIT_BT:
    default:
        break;
    }
}

const char *P0_RpmMap_GetStateName(p0_state_t state)
{
    switch (state)
    {
    case P0_STATE_WAIT_BT:
        return "WAIT_BT";
    case P0_STATE_READY:
        return "READY";
    case P0_STATE_PREPARE:
        return "PREPARE";
    case P0_STATE_RAMP:
        return "RAMP";
    case P0_STATE_STABILIZE:
        return "STABILIZE";
    case P0_STATE_MEASURE:
        return "MEASURE";
    case P0_STATE_REST:
        return "REST";
    case P0_STATE_DONE:
        return "DONE";
    case P0_STATE_ABORTED:
        return "ABORTED";
    case P0_STATE_FAULT:
        return "FAULT";
    default:
        return "UNKNOWN";
    }
}

static void P0_LoadDefaultSweep(uint8_t extended)
{
    uint16_t end = (extended != 0U) ? P0_EXTENDED_SWEEP_END : P0_DEFAULT_SWEEP_END;
    (void)P0_SetSweep(P0_DEFAULT_SWEEP_START, end, P0_DEFAULT_SWEEP_STEP);
}

static uint8_t P0_SetSweep(uint16_t start, uint16_t end, uint16_t step)
{
    uint16_t cmd;
    uint8_t count = 0U;

    if ((start < P0_DSHOT_MIN_THROTTLE) ||
        (end > P0_DSHOT_MAX_THROTTLE) ||
        (step == 0U) ||
        (start > end))
    {
        return 0U;
    }

    for (cmd = start; cmd <= end; cmd = (uint16_t)(cmd + step))
    {
        if (count >= P0_MAX_SWEEP_POINTS)
        {
            return 0U;
        }

        g_p0.sweep[count++] = cmd;
        if ((uint16_t)(cmd + step) < cmd)
        {
            return 0U;
        }
    }

    if (count == 0U)
    {
        return 0U;
    }

    g_p0.sweep_count = count;
    g_p0.step_index = 0U;
    g_p0.preview_dshot_value = g_p0.sweep[0];
    return 1U;
}

static void P0_StartMapping(uint32_t now_ms)
{
    char line[160];
    int len;

    if ((g_p0.state != P0_STATE_READY) &&
        (g_p0.state != P0_STATE_DONE) &&
        (g_p0.state != P0_STATE_ABORTED))
    {
        (void)P0_UartWrite("# warn start_ignored reason=state_not_ready\r\n");
        return;
    }

    P0_UartTx_ResetQueue();
    g_p0.session_start_tick = now_ms;
    g_p0.last_csv_tick = now_ms;
    g_p0.last_measure_sample_tick = 0U;
    g_p0.step_index = 0U;
    g_p0.repeat_requested = 0U;
    g_p0.summary_sent = 0U;
    g_p0.current_trip_start_tick = 0U;
    g_p0.fault_latched = 0U;
    g_p0.fault_reason = NULL;
    g_p0.csv_header_sent = 0U;
    g_p0.active_zero_offset_voltage = g_p0.baseline_zero_offset_voltage;
    P0_ClearRpmReadings();
    P0_MeasureStats_Reset();

    if (g_p0.board == P0_BOARD_UNKNOWN)
    {
        (void)P0_UartWrite("# warn board=UNKNOWN action=allowed\r\n");
    }
    if (g_p0.load == P0_LOAD_UNKNOWN)
    {
        (void)P0_UartWrite("# warn load=UNKNOWN action=allowed\r\n");
    }

    len = snprintf(line,
                   sizeof(line),
                   "# p0_start board=%s load=%s count=%u\r\n",
                   P0_BoardName(),
                   P0_LoadName(),
                   (unsigned int)g_p0.sweep_count);
    if (len > 0)
    {
        (void)P0_UartWrite(line);
    }
    P0_SendCsvHeaderOnce();
    P0_EnterState(P0_STATE_PREPARE);
}

static void P0_BeginCurrentStep(void)
{
    if (g_p0.step_index >= g_p0.sweep_count)
    {
        P0_EnterState(P0_STATE_DONE);
        return;
    }

    g_p0.target_dshot_value = g_p0.sweep[g_p0.step_index];
    g_p0.ramp_duration_ms = P0_RAMP_MS;
#if (P0_CONTINUOUS_SWEEP != 0U)
    g_p0.ramp_start_dshot_value = g_p0.current_dshot_value;
#elif (P0_JUMP_TO_TARGET_FROM_STOP != 0U)
    if ((g_p0.current_dshot_value == 0U) &&
        (g_p0.target_dshot_value > P0_HIGH_START_RAMP_THRESHOLD) &&
        (P0_HIGH_START_RAMP_START < g_p0.target_dshot_value))
    {
        g_p0.ramp_start_dshot_value = P0_HIGH_START_RAMP_START;
        g_p0.ramp_duration_ms = P0_HIGH_START_RAMP_MS;
    }
    else
    {
        g_p0.ramp_start_dshot_value = g_p0.target_dshot_value;
    }
#else
    g_p0.ramp_start_dshot_value = 0U;
#endif
    P0_ClearRpmReadings();
    P0_MeasureStats_Reset();
    g_p0.summary_sent = 0U;
    if (g_p0.ramp_start_dshot_value == g_p0.target_dshot_value)
    {
        P0_EnterState(P0_STATE_STABILIZE);
    }
    else
    {
        P0_EnterState(P0_STATE_RAMP);
    }
}

#if (P0_REQUIRE_NEXT_TO_ADVANCE == 0U)
static void P0_FinishCurrentPoint(void)
{
    P0_SendCurrentStepSummaryOnce(P0_CurrentStepSummaryStatus());
    P0_EnterState(P0_STATE_REST);
}
#endif

static void P0_SendCurrentStepSummaryOnce(const char *status)
{
    if (g_p0.summary_sent == 0U)
    {
        P0_SendStepSummary(status);
        g_p0.summary_sent = 1U;
    }
}

#if (P0_REQUIRE_NEXT_TO_ADVANCE != 0U)
static void P0_AdvanceToNextStepWithoutStop(void)
{
    P0_SendCurrentStepSummaryOnce(P0_CurrentStepSummaryStatus());

    if (g_p0.repeat_requested != 0U)
    {
        g_p0.repeat_requested = 0U;
        P0_BeginCurrentStep();
        return;
    }

    g_p0.step_index++;
    if (g_p0.step_index >= g_p0.sweep_count)
    {
        P0_EnterState(P0_STATE_DONE);
    }
    else
    {
        P0_BeginCurrentStep();
    }
}
#endif

static void P0_AdvanceAfterRest(void)
{
    if (g_p0.repeat_requested != 0U)
    {
        g_p0.repeat_requested = 0U;
        P0_BeginCurrentStep();
        return;
    }

    g_p0.step_index++;
    if (g_p0.step_index >= g_p0.sweep_count)
    {
        P0_EnterState(P0_STATE_DONE);
    }
    else
    {
        P0_BeginCurrentStep();
    }
}

static void P0_EnterState(p0_state_t state)
{
    char line[160];
    int len;

    g_p0.state = state;
    g_p0.state_enter_tick = HAL_GetTick();

    switch (state)
    {
    case P0_STATE_WAIT_BT:
    case P0_STATE_READY:
    case P0_STATE_PREPARE:
    case P0_STATE_REST:
    case P0_STATE_DONE:
    case P0_STATE_ABORTED:
    case P0_STATE_FAULT:
        g_p0.current_dshot_value = 0U;
        P0_ForceMotorStopFrame();
        break;

    case P0_STATE_RAMP:
        g_p0.active_zero_offset_voltage = g_p0.baseline_zero_offset_voltage;
        g_p0.current_dshot_value = g_p0.ramp_start_dshot_value;
        len = snprintf(line,
                       sizeof(line),
                       "# p0_ramp_start board=%s load=%s index=%u from=%u to=%u ms=%lu\r\n",
                       P0_BoardName(),
                       P0_LoadName(),
                       (unsigned int)g_p0.step_index,
                       (unsigned int)g_p0.ramp_start_dshot_value,
                       (unsigned int)g_p0.target_dshot_value,
                       (unsigned long)g_p0.ramp_duration_ms);
        if (len > 0)
        {
            (void)P0_UartWrite(line);
        }
        break;

    case P0_STATE_STABILIZE:
        g_p0.current_dshot_value = g_p0.target_dshot_value;
        len = snprintf(line,
                       sizeof(line),
                       "# p0_step_start board=%s load=%s index=%u cmd=%u\r\n",
                       P0_BoardName(),
                       P0_LoadName(),
                       (unsigned int)g_p0.step_index,
                       (unsigned int)g_p0.target_dshot_value);
        if (len > 0)
        {
            (void)P0_UartWrite(line);
        }
        break;

    case P0_STATE_MEASURE:
        g_p0.current_dshot_value = g_p0.target_dshot_value;
        P0_ClearRpmReadings();
        P0_MeasureStats_Reset();
        g_p0.last_measure_sample_tick = 0U;
#if (P0_ENABLE_UART_RPM_INPUT != 0U)
        len = snprintf(line,
                       sizeof(line),
                       "# rpm_prompt board=%s load=%s cmd=%u readings_needed=%u\r\n",
                       P0_BoardName(),
                       P0_LoadName(),
                       (unsigned int)g_p0.target_dshot_value,
                       (unsigned int)P0_RPM_REQUIRED_READINGS);
#else
        len = snprintf(line,
                       sizeof(line),
                       "# throttle_hold board=%s load=%s index=%u cmd=%u action=measure_on_paper_send_NEXT\r\n",
                       P0_BoardName(),
                       P0_LoadName(),
                       (unsigned int)g_p0.step_index,
                       (unsigned int)g_p0.target_dshot_value);
#endif
        if (len > 0)
        {
            (void)P0_UartWrite(line);
        }
        break;

    default:
        g_p0.current_dshot_value = 0U;
        break;
    }

    if (state == P0_STATE_DONE)
    {
        len = snprintf(line,
                       sizeof(line),
                       "# p0_done board=%s load=%s points=%u\r\n",
                       P0_BoardName(),
                       P0_LoadName(),
                       (unsigned int)g_p0.sweep_count);
        if (len > 0)
        {
            (void)P0_UartWrite(line);
        }
    }

    len = snprintf(line,
                   sizeof(line),
                   "# state_change state=%u name=%s\r\n",
                   (unsigned int)state,
                   P0_RpmMap_GetStateName(state));
    if (len > 0)
    {
        (void)P0_UartWrite(line);
    }
}

static uint32_t P0_ElapsedMs(uint32_t now_ms, uint32_t since_ms)
{
    uint32_t elapsed_ms = now_ms - since_ms;

    /* A state change can take a HAL tick after the task's now_ms snapshot. */
    if (elapsed_ms > 0x7FFFFFFFUL)
    {
        return 0U;
    }

    return elapsed_ms;
}

static uint8_t P0_IsBluetoothConnected(void)
{
    return (HAL_GPIO_ReadPin(BT_STATE_GPIO_Port, BT_STATE_Pin) == P0_BT_CONNECTED_LEVEL) ? 1U : 0U;
}

static uint8_t P0_IsStoppedState(void)
{
    return ((g_p0.state == P0_STATE_WAIT_BT) ||
            (g_p0.state == P0_STATE_READY) ||
            (g_p0.state == P0_STATE_PREPARE) ||
            (g_p0.state == P0_STATE_REST) ||
            (g_p0.state == P0_STATE_DONE) ||
            (g_p0.state == P0_STATE_ABORTED) ||
            (g_p0.state == P0_STATE_FAULT)) ? 1U : 0U;
}

static uint8_t P0_IsActiveState(void)
{
    return ((g_p0.state == P0_STATE_RAMP) ||
            (g_p0.state == P0_STATE_STABILIZE) ||
            (g_p0.state == P0_STATE_MEASURE)) ? 1U : 0U;
}

static void P0_ProcessAdcAverage(P0_AdcProcessed_t *out)
{
    uint32_t i;
    uint32_t sum_i = 0U;
    uint32_t sum_vbat = 0U;
    float adc_to_volt = ADC_REF_VOLTAGE / ADC_MAX_COUNTS;

    if (out == NULL)
    {
        return;
    }

    for (i = 0U; i < P0_ADC_DMA_BUFFER_LENGTH; i += P0_ADC_CHANNEL_COUNT)
    {
        sum_i += g_adc_dma_buffer[i];
        sum_vbat += g_adc_dma_buffer[i + 1U];
    }

    out->adc_i_raw = (uint16_t)(sum_i / P0_ADC_DMA_SAMPLES_PER_CHANNEL);
    out->adc_vbat_raw = (uint16_t)(sum_vbat / P0_ADC_DMA_SAMPLES_PER_CHANNEL);
    out->v_i_sense = (float)out->adc_i_raw * adc_to_volt;
    out->v_vbat_adc = (float)out->adc_vbat_raw * adc_to_volt;
    out->vbat_V = out->v_vbat_adc * VBAT_DIVIDER_RATIO;
}

static void P0_UpdateZeroOffset(const P0_AdcProcessed_t *adc_data, uint32_t now_ms)
{
    float delta;

    if ((adc_data == NULL) || (P0_IsStoppedState() == 0U) || (g_p0.current_dshot_value != 0U))
    {
        return;
    }

    if (P0_ElapsedMs(now_ms, g_p0.last_zero_track_tick) < P0_ZERO_TRACK_DELAY_MS)
    {
        return;
    }

    g_p0.last_zero_track_tick = now_ms;
    delta = adc_data->v_i_sense - g_p0.baseline_zero_offset_voltage;
    g_p0.baseline_zero_offset_voltage += delta * P0_ZERO_TRACK_ALPHA_STOP;
    g_p0.active_zero_offset_voltage = g_p0.baseline_zero_offset_voltage;
}

static void P0_UpdateCurrentDerived(P0_AdcProcessed_t *adc_data)
{
    float signed_delta_v;
    float current_a;

    if (adc_data == NULL)
    {
        return;
    }

    adc_data->active_zero_offset_V = g_p0.active_zero_offset_voltage;
    adc_data->delta_i_V = adc_data->v_i_sense - adc_data->active_zero_offset_V;

#if (P0_CURRENT_SIGN_INVERT != 0U)
    signed_delta_v = -adc_data->delta_i_V;
#else
    signed_delta_v = adc_data->delta_i_V;
#endif

    current_a = signed_delta_v * CURRENT_SCALE_A_PER_V;
    if (current_a < 0.0f)
    {
        current_a = 0.0f;
    }

    adc_data->current_A = current_a;
    adc_data->power_W = adc_data->vbat_V * adc_data->current_A;
}

static void P0_MeasureStats_Reset(void)
{
    memset(&g_p0.stats, 0, sizeof(g_p0.stats));
}

static void P0_MeasureStats_Accumulate(uint32_t now_ms)
{
    if (g_p0.state != P0_STATE_MEASURE)
    {
        return;
    }

    if ((g_p0.last_measure_sample_tick != 0U) &&
        (P0_ElapsedMs(now_ms, g_p0.last_measure_sample_tick) < P0_MEASURE_SAMPLE_INTERVAL_MS))
    {
        return;
    }

    g_p0.last_measure_sample_tick = now_ms;
    g_p0.stats.sum_vbat += g_p0.adc.vbat_V;
    g_p0.stats.sum_current += g_p0.adc.current_A;
    g_p0.stats.sum_power += g_p0.adc.power_W;
    g_p0.stats.sum_vbat2 += g_p0.adc.vbat_V * g_p0.adc.vbat_V;
    g_p0.stats.sum_current2 += g_p0.adc.current_A * g_p0.adc.current_A;
    g_p0.stats.sum_power2 += g_p0.adc.power_W * g_p0.adc.power_W;
    g_p0.stats.count++;
}

static float P0_StatsMean(float sum, uint32_t count)
{
    return (count == 0U) ? 0.0f : (sum / (float)count);
}

static float P0_StatsStd(float sum, float sum2, uint32_t count)
{
    float mean;
    float variance;

    if (count == 0U)
    {
        return 0.0f;
    }

    mean = sum / (float)count;
    variance = (sum2 / (float)count) - (mean * mean);
    if (variance < 0.0f)
    {
        variance = 0.0f;
    }

    return sqrtf(variance);
}

static void P0_SafetyTask(uint32_t now_ms)
{
    if ((g_p0.fault_latched != 0U) || (P0_IsActiveState() == 0U))
    {
        g_p0.current_trip_start_tick = 0U;
        return;
    }

#if (P0_ENABLE_CURRENT_TRIP != 0U)
    if (g_p0.adc.current_A >= P0_CURRENT_TRIP_A)
    {
        if (g_p0.current_trip_start_tick == 0U)
        {
            g_p0.current_trip_start_tick = now_ms;
        }
        else if (P0_ElapsedMs(now_ms, g_p0.current_trip_start_tick) >= P0_CURRENT_TRIP_HOLD_MS)
        {
            P0_TriggerFault("overcurrent");
            return;
        }
    }
    else
    {
        g_p0.current_trip_start_tick = 0U;
    }
#endif

#if (P0_ENABLE_VBAT_TRIP != 0U)
    if (g_p0.adc.vbat_V < P0_VBAT_MIN_V)
    {
        P0_TriggerFault("undervoltage");
    }
#endif
}

static void P0_TriggerFault(const char *reason)
{
    char line[192];
    char current_str[20];
    char vbat_str[20];
    p0_state_t fault_state = g_p0.state;
    uint8_t fault_index = g_p0.step_index;
    uint16_t fault_target = g_p0.target_dshot_value;
    uint16_t fault_dshot = g_p0.current_dshot_value;
    int len;

    if (g_p0.fault_latched != 0U)
    {
        return;
    }

    g_p0.fault_latched = 1U;
    g_p0.fault_reason = reason;
    P0_ForceMotorStopFrame();
    P0_FormatFloat3(current_str, sizeof(current_str), g_p0.adc.current_A);
    P0_FormatFloat3(vbat_str, sizeof(vbat_str), g_p0.adc.vbat_V);
    len = snprintf(line,
                   sizeof(line),
                   "# fault reason=%s state=%s index=%u cmd=%u dshot_cmd=%u current_A=%s vbat=%s\r\n",
                   (reason != NULL) ? reason : "unknown",
                   P0_RpmMap_GetStateName(fault_state),
                   (unsigned int)fault_index,
                   (unsigned int)fault_target,
                   (unsigned int)fault_dshot,
                   current_str,
                   vbat_str);
    if (len > 0)
    {
        (void)P0_UartWrite(line);
    }

    P0_EnterState(P0_STATE_FAULT);
}

static void P0_Abort(const char *reason)
{
    char line[160];
    p0_state_t abort_state = g_p0.state;
    uint8_t abort_index = g_p0.step_index;
    uint16_t abort_target = g_p0.target_dshot_value;
    uint16_t abort_dshot = g_p0.current_dshot_value;
    int len;

    P0_ForceMotorStopFrame();
    if (g_p0.state == P0_STATE_FAULT)
    {
        (void)P0_UartWrite("# abort_ignored reason=fault_latched\r\n");
        return;
    }

    len = snprintf(line,
                   sizeof(line),
                   "# abort reason=%s state=%s index=%u cmd=%u dshot_cmd=%u\r\n",
                   (reason != NULL) ? reason : "user_stop",
                   P0_RpmMap_GetStateName(abort_state),
                   (unsigned int)abort_index,
                   (unsigned int)abort_target,
                   (unsigned int)abort_dshot);
    if (len > 0)
    {
        (void)P0_UartWrite(line);
    }
    P0_EnterState(P0_STATE_ABORTED);
}

static void P0_ButtonTask(uint32_t now_ms)
{
    uint8_t raw_pressed;
    uint32_t held_ms;

    raw_pressed = (HAL_GPIO_ReadPin(BTN_THROTTLE_GPIO_Port, BTN_THROTTLE_Pin) == GPIO_PIN_RESET) ? 1U : 0U;

    if (raw_pressed != g_p0.button_raw_pressed)
    {
        g_p0.button_raw_pressed = raw_pressed;
        g_p0.button_change_tick = now_ms;
    }

    if (P0_ElapsedMs(now_ms, g_p0.button_change_tick) < P0_BUTTON_DEBOUNCE_MS)
    {
        return;
    }

    if (raw_pressed != g_p0.button_stable_pressed)
    {
        g_p0.button_stable_pressed = raw_pressed;
        if (raw_pressed != 0U)
        {
            g_p0.button_press_tick = now_ms;
            g_p0.button_long_handled = 0U;
            if (P0_IsActiveState() != 0U)
            {
                g_p0.button_long_handled = 1U;
                P0_Abort("button");
            }
        }
        else if (g_p0.button_long_handled == 0U)
        {
            if ((g_p0.state == P0_STATE_READY) ||
                (g_p0.state == P0_STATE_DONE) ||
                (g_p0.state == P0_STATE_ABORTED))
            {
                P0_CyclePreviewCommand();
            }
        }
    }

    if ((g_p0.button_stable_pressed != 0U) && (g_p0.button_long_handled == 0U))
    {
        held_ms = P0_ElapsedMs(now_ms, g_p0.button_press_tick);
        if (held_ms >= P0_BUTTON_LONG_PRESS_MS)
        {
            g_p0.button_long_handled = 1U;
            if ((g_p0.state == P0_STATE_READY) ||
                (g_p0.state == P0_STATE_DONE) ||
                (g_p0.state == P0_STATE_ABORTED))
            {
                P0_StartMapping(now_ms);
            }
            else if (P0_IsActiveState() != 0U)
            {
                P0_Abort("button");
            }
        }
    }
}

static void P0_CyclePreviewCommand(void)
{
    char line[80];
    int len;

    g_p0.preview_dshot_value = (uint16_t)(g_p0.preview_dshot_value + P0_DEFAULT_SWEEP_STEP);
    if ((g_p0.preview_dshot_value > P0_DEFAULT_SWEEP_END) ||
        (g_p0.preview_dshot_value < P0_DEFAULT_SWEEP_START))
    {
        g_p0.preview_dshot_value = P0_DEFAULT_SWEEP_START;
    }

    len = snprintf(line,
                   sizeof(line),
                   "# preview cmd=%u use_CMD_to_run_single_point\r\n",
                   (unsigned int)g_p0.preview_dshot_value);
    if (len > 0)
    {
        (void)P0_UartWrite(line);
    }
}

static void P0_StatusLed_Set(uint8_t on)
{
    HAL_GPIO_WritePin(STATUS_LED_GPIO_Port,
                      STATUS_LED_Pin,
                      (on != 0U) ? P0_STATUS_LED_ON_LEVEL : P0_STATUS_LED_OFF_LEVEL);
}

static void P0_StatusLed_Update(uint32_t now_ms)
{
    uint32_t phase_ms;

    switch (g_p0.state)
    {
    case P0_STATE_WAIT_BT:
        phase_ms = now_ms % 2000U;
        P0_StatusLed_Set(((phase_ms < 80U) || ((phase_ms >= 250U) && (phase_ms < 330U))) ? 1U : 0U);
        break;
    case P0_STATE_READY:
    case P0_STATE_DONE:
        phase_ms = now_ms % 1000U;
        P0_StatusLed_Set((phase_ms < 60U) ? 1U : 0U);
        break;
    case P0_STATE_PREPARE:
    case P0_STATE_REST:
        phase_ms = now_ms % 1000U;
        P0_StatusLed_Set((phase_ms < 150U) ? 1U : 0U);
        break;
    case P0_STATE_RAMP:
    case P0_STATE_STABILIZE:
        phase_ms = now_ms % 500U;
        P0_StatusLed_Set((phase_ms < 250U) ? 1U : 0U);
        break;
    case P0_STATE_MEASURE:
        P0_StatusLed_Set(1U);
        break;
    case P0_STATE_ABORTED:
    case P0_STATE_FAULT:
    default:
        phase_ms = now_ms % 250U;
        P0_StatusLed_Set((phase_ms < 125U) ? 1U : 0U);
        break;
    }
}

static uint16_t P0_ClampDshotThrottle(uint16_t cmd)
{
    if (cmd == 0U)
    {
        return 0U;
    }

    if (cmd < P0_DSHOT_MIN_THROTTLE)
    {
        return P0_DSHOT_MIN_THROTTLE;
    }

    if (cmd > P0_DSHOT_MAX_THROTTLE)
    {
        return P0_DSHOT_MAX_THROTTLE;
    }

    return cmd;
}

static uint16_t P0_Dshot_BuildPacket(uint16_t throttle_value)
{
    uint16_t packet;
    uint16_t checksum;
    uint16_t csum_data;
    uint8_t i;

    packet = (uint16_t)((throttle_value << 1U) | (P0_DSHOT_TELEMETRY_BIT & 0x1U));
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

static void P0_Dshot_PrepareFrame(uint16_t throttle_value)
{
    uint16_t packet;
    uint8_t i;

    packet = P0_Dshot_BuildPacket(P0_ClampDshotThrottle(throttle_value));

    for (i = 0U; i < P0_DSHOT_FRAME_BITS; i++)
    {
        g_dshot_dma_buffer[i] = ((packet & 0x8000U) != 0U) ? P0_DSHOT_BIT_1_HIGH_TICKS : P0_DSHOT_BIT_0_HIGH_TICKS;
        packet <<= 1U;
    }

    for (i = P0_DSHOT_FRAME_BITS; i < P0_DSHOT_DMA_BUFFER_LENGTH; i++)
    {
        g_dshot_dma_buffer[i] = 0U;
    }
}

static void P0_Dshot_TriggerFrame(uint16_t throttle_value)
{
    HAL_StatusTypeDef status;

    if (g_p0.dshot_dma_busy != 0U)
    {
        return;
    }

    P0_Dshot_PrepareFrame(throttle_value);
    g_p0.dshot_dma_busy = 1U;

    __HAL_TIM_DISABLE(&htim4);
    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, g_dshot_dma_buffer[0]);

    status = HAL_TIM_PWM_Start_DMA(&htim4,
                                   TIM_CHANNEL_3,
                                   (const uint32_t *)&g_dshot_dma_buffer[1],
                                   (uint16_t)(P0_DSHOT_DMA_BUFFER_LENGTH - 1U));
    if (status != HAL_OK)
    {
        g_p0.dshot_dma_busy = 0U;
        g_p0.current_dshot_value = 0U;
        P0_TriggerFault("dshot_dma");
    }
}

static void P0_Dshot_Service(uint32_t now_ms)
{
    if (P0_ElapsedMs(now_ms, g_p0.last_dshot_send_tick) < P0_DSHOT_SEND_INTERVAL_MS)
    {
        return;
    }

    if (g_p0.dshot_dma_busy != 0U)
    {
        return;
    }

    g_p0.last_dshot_send_tick = now_ms;
    P0_Dshot_TriggerFrame(g_p0.current_dshot_value);
}

static void P0_ForceMotorStopFrame(void)
{
    g_p0.current_dshot_value = 0U;

    if (g_p0.dshot_dma_busy != 0U)
    {
        (void)HAL_TIM_PWM_Stop_DMA(&htim4, TIM_CHANNEL_3);
        g_p0.dshot_dma_busy = 0U;
    }

    __HAL_TIM_DISABLE(&htim4);
    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);
    P0_Dshot_TriggerFrame(0U);
}

static void P0_UpdateRampCommand(uint32_t now_ms)
{
    uint32_t elapsed_ms;
    uint32_t delta;
    uint32_t cmd;

    if (g_p0.state != P0_STATE_RAMP)
    {
        return;
    }

    elapsed_ms = P0_ElapsedMs(now_ms, g_p0.state_enter_tick);
    if ((g_p0.ramp_duration_ms == 0U) || (elapsed_ms >= g_p0.ramp_duration_ms))
    {
        g_p0.current_dshot_value = g_p0.target_dshot_value;
        return;
    }

    if (g_p0.target_dshot_value <= g_p0.ramp_start_dshot_value)
    {
        g_p0.current_dshot_value = g_p0.target_dshot_value;
        return;
    }

    delta = (uint32_t)g_p0.target_dshot_value - (uint32_t)g_p0.ramp_start_dshot_value;
    cmd = (uint32_t)g_p0.ramp_start_dshot_value + ((delta * elapsed_ms) / g_p0.ramp_duration_ms);
    g_p0.current_dshot_value = P0_ClampDshotThrottle((uint16_t)cmd);
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
    g_p0.dshot_dma_busy = 0U;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        g_uart_rx_active = 0U;
        P0_ProcessReceivedByte(g_uart_rx_byte);
        P0_UartRx_Start();
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        g_uart_tx_busy = 0U;
        P0_UartTx_StartNext();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        g_uart_rx_active = 0U;
        g_uart_tx_busy = 0U;
        P0_UartTx_ResetQueue();
        P0_UartRx_Start();
    }
}

static void P0_UartRx_Start(void)
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

static void P0_ProcessReceivedByte(uint8_t byte)
{
    char c = (char)byte;

    if ((c == '\r') || (c == '\n'))
    {
        if (g_uart_cmd_index > 0U)
        {
            g_uart_cmd_buffer[g_uart_cmd_index] = '\0';
            if (g_uart_cmd_pending == 0U)
            {
                memcpy(g_uart_pending_cmd, g_uart_cmd_buffer, (size_t)g_uart_cmd_index + 1U);
                g_uart_cmd_pending = 1U;
            }
            g_uart_cmd_index = 0U;
        }
        return;
    }

    if ((c >= 32) && (c <= 126))
    {
        if (g_uart_cmd_index < (uint8_t)(sizeof(g_uart_cmd_buffer) - 1U))
        {
            g_uart_cmd_buffer[g_uart_cmd_index++] = c;
        }
        else
        {
            g_uart_cmd_index = 0U;
        }
    }
}

static void P0_ProcessPendingCommand(void)
{
    char cmd[P0_UART_CMD_BUFFER_SIZE];
    uint32_t primask;

    if (g_uart_cmd_pending == 0U)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    strncpy(cmd, g_uart_pending_cmd, sizeof(cmd));
    cmd[sizeof(cmd) - 1U] = '\0';
    g_uart_cmd_pending = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    P0_HandleCommand(cmd);
}

static void P0_HandleCommand(const char *cmd)
{
    char upper[P0_UART_CMD_BUFFER_SIZE];
    char *cursor;
    char *verb;
    char *arg1;
    char *arg2;
    char *arg3;
    uint16_t value16;

    if (cmd == NULL)
    {
        return;
    }

    P0_ToUpperString(upper, sizeof(upper), cmd);
    cursor = upper;
    verb = P0_NextToken(&cursor);
    if (verb == NULL)
    {
        return;
    }

    if (strcmp(verb, "HELP") == 0)
    {
        P0_SendHelp();
    }
    else if (strcmp(verb, "STATUS") == 0)
    {
        P0_SendStatus();
    }
    else if (strcmp(verb, "START") == 0)
    {
        P0_StartMapping(HAL_GetTick());
    }
    else if ((strcmp(verb, "STOP") == 0) || (strcmp(verb, "ABORT") == 0))
    {
        P0_Abort("user_stop");
    }
    else if (strcmp(verb, "BOARD") == 0)
    {
        arg1 = P0_NextToken(&cursor);
        if ((arg1 != NULL) && (strcmp(arg1, "SI") == 0))
        {
            g_p0.board = P0_BOARD_SI;
            (void)P0_UartWrite("# board_set board=Si\r\n");
        }
        else if ((arg1 != NULL) && (strcmp(arg1, "GAN") == 0))
        {
            g_p0.board = P0_BOARD_GAN;
            (void)P0_UartWrite("# board_set board=GaN\r\n");
        }
        else
        {
            (void)P0_UartWrite("# error cmd=BOARD expected=Si_or_GaN\r\n");
        }
    }
    else if (strcmp(verb, "LOAD") == 0)
    {
        arg1 = P0_NextToken(&cursor);
        if ((arg1 != NULL) && (strcmp(arg1, "PROP") == 0))
        {
            g_p0.load = P0_LOAD_PROP;
            (void)P0_UartWrite("# load_set load=prop\r\n");
        }
        else if ((arg1 != NULL) && (strcmp(arg1, "NOPROP") == 0))
        {
            g_p0.load = P0_LOAD_NOPROP;
            (void)P0_UartWrite("# load_set load=noProp\r\n");
        }
        else
        {
            (void)P0_UartWrite("# error cmd=LOAD expected=prop_or_noProp\r\n");
        }
    }
    else if (strcmp(verb, "RPM") == 0)
    {
#if (P0_ENABLE_UART_RPM_INPUT != 0U)
        uint32_t value32;

        arg1 = P0_NextToken(&cursor);
        if ((arg1 == NULL) || (P0_ParseU32(arg1, &value32) == 0U))
        {
            (void)P0_UartWrite("# error cmd=RPM expected=number\r\n");
        }
        else if (g_p0.state != P0_STATE_MEASURE)
        {
            (void)P0_UartWrite("# warn rpm_ignored reason=not_measuring\r\n");
        }
        else if (g_p0.rpm_count >= P0_RPM_MAX_READINGS)
        {
            (void)P0_UartWrite("# warn rpm_ignored reason=full\r\n");
        }
        else
        {
            g_p0.rpm_readings[g_p0.rpm_count++] = value32;
            P0_SendRpmAddedLine(value32);
        }
#else
        (void)P0_UartWrite("# warn rpm_ignored reason=uart_rpm_disabled\r\n");
#endif
    }
    else if (strcmp(verb, "CLEAR_RPM") == 0)
    {
#if (P0_ENABLE_UART_RPM_INPUT != 0U)
        P0_ClearRpmReadings();
        (void)P0_UartWrite("# rpm_cleared\r\n");
#else
        (void)P0_UartWrite("# warn clear_rpm_ignored reason=uart_rpm_disabled\r\n");
#endif
    }
    else if (strcmp(verb, "NEXT") == 0)
    {
#if (P0_REQUIRE_NEXT_TO_ADVANCE != 0U)
        if (g_p0.state == P0_STATE_MEASURE)
        {
            P0_AdvanceToNextStepWithoutStop();
        }
        else if (P0_IsActiveState() != 0U)
        {
            (void)P0_UartWrite("# warn next_ignored reason=not_measuring\r\n");
        }
        else
        {
            (void)P0_UartWrite("# warn next_ignored reason=not_active\r\n");
        }
#else
        if (P0_IsActiveState() != 0U)
        {
            P0_FinishCurrentPoint();
        }
        else
        {
            (void)P0_UartWrite("# warn next_ignored reason=not_active\r\n");
        }
#endif
    }
    else if (strcmp(verb, "REPEAT") == 0)
    {
#if (P0_REQUIRE_NEXT_TO_ADVANCE != 0U)
        if (g_p0.state == P0_STATE_MEASURE)
        {
            (void)P0_UartWrite("# repeat_current action=hold_without_stop\r\n");
            P0_SendCurrentStepSummaryOnce(P0_CurrentStepSummaryStatus());
            P0_BeginCurrentStep();
        }
        else if (P0_IsActiveState() != 0U)
        {
            (void)P0_UartWrite("# warn repeat_ignored reason=not_measuring\r\n");
        }
        else
        {
            (void)P0_UartWrite("# warn repeat_ignored reason=not_active\r\n");
        }
#else
        if (P0_IsActiveState() != 0U)
        {
            g_p0.repeat_requested = 1U;
            (void)P0_UartWrite("# repeat_current enabled=1\r\n");
            if (g_p0.state == P0_STATE_MEASURE)
            {
                P0_FinishCurrentPoint();
            }
            else
            {
                P0_EnterState(P0_STATE_REST);
            }
        }
        else
        {
            (void)P0_UartWrite("# warn repeat_ignored reason=not_active\r\n");
        }
#endif
    }
    else if (strcmp(verb, "SWEEP") == 0)
    {
        arg1 = P0_NextToken(&cursor);
        arg2 = P0_NextToken(&cursor);
        arg3 = P0_NextToken(&cursor);
        if ((g_p0.state != P0_STATE_READY) &&
            (g_p0.state != P0_STATE_DONE) &&
            (g_p0.state != P0_STATE_ABORTED))
        {
            (void)P0_UartWrite("# error cmd=SWEEP reason=active_state\r\n");
        }
        else if ((arg1 == NULL) || (arg2 == NULL) || (arg3 == NULL) ||
            (P0_ParseU16(arg1, &value16) == 0U))
        {
            (void)P0_UartWrite("# error cmd=SWEEP expected=start_end_step\r\n");
        }
        else
        {
            uint16_t start = value16;
            uint16_t end;
            uint16_t step;
            if ((P0_ParseU16(arg2, &end) != 0U) &&
                (P0_ParseU16(arg3, &step) != 0U) &&
                (P0_SetSweep(start, end, step) != 0U))
            {
                P0_SendSweepSetLine();
            }
            else
            {
                (void)P0_UartWrite("# error cmd=SWEEP reason=invalid_range\r\n");
            }
        }
    }
    else if (strcmp(verb, "EXTEND") == 0)
    {
        arg1 = P0_NextToken(&cursor);
        if ((g_p0.state != P0_STATE_READY) &&
            (g_p0.state != P0_STATE_DONE) &&
            (g_p0.state != P0_STATE_ABORTED))
        {
            (void)P0_UartWrite("# error cmd=EXTEND reason=active_state\r\n");
        }
        else if ((arg1 != NULL) && (strcmp(arg1, "ON") == 0))
        {
            P0_LoadDefaultSweep(1U);
            (void)P0_UartWrite("# extend_set enabled=1\r\n");
            P0_SendSweepSetLine();
        }
        else if ((arg1 != NULL) && (strcmp(arg1, "OFF") == 0))
        {
            P0_LoadDefaultSweep(0U);
            (void)P0_UartWrite("# extend_set enabled=0\r\n");
            P0_SendSweepSetLine();
        }
        else
        {
            (void)P0_UartWrite("# error cmd=EXTEND expected=ON_or_OFF\r\n");
        }
    }
    else if (strcmp(verb, "CMD") == 0)
    {
        arg1 = P0_NextToken(&cursor);
        if ((g_p0.state != P0_STATE_READY) &&
            (g_p0.state != P0_STATE_DONE) &&
            (g_p0.state != P0_STATE_ABORTED))
        {
            (void)P0_UartWrite("# error cmd=CMD reason=active_state\r\n");
        }
        else if ((arg1 != NULL) &&
            (P0_ParseU16(arg1, &value16) != 0U) &&
            (value16 >= P0_DSHOT_MIN_THROTTLE) &&
            (value16 <= P0_DSHOT_MAX_THROTTLE))
        {
            g_p0.sweep[0] = value16;
            g_p0.sweep_count = 1U;
            g_p0.step_index = 0U;
            g_p0.preview_dshot_value = value16;
            P0_SendSweepSetLine();
        }
        else
        {
            (void)P0_UartWrite("# error cmd=CMD reason=invalid_value\r\n");
        }
    }
    else
    {
        (void)P0_UartWrite("# error reason=unknown_command use=HELP\r\n");
    }
}

static uint8_t P0_UartWrite(const char *text)
{
    uint16_t free_space;
    uint32_t primask;
    size_t len;
    size_t i;

    if ((text == NULL) || (P0_UartCanSend() == 0U))
    {
        return 0U;
    }

    len = strlen(text);
    if ((len == 0U) || (len >= P0_UART_TX_BUFFER_SIZE))
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if (g_uart_tx_head >= g_uart_tx_tail)
    {
        free_space = (uint16_t)(P0_UART_TX_BUFFER_SIZE - (g_uart_tx_head - g_uart_tx_tail) - 1U);
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
        g_uart_tx_head = (uint16_t)((g_uart_tx_head + 1U) % P0_UART_TX_BUFFER_SIZE);
    }
    if (primask == 0U)
    {
        __enable_irq();
    }

    P0_UartTx_StartNext();
    return 1U;
}

static uint8_t P0_UartCanSend(void)
{
    if (P0_ElapsedMs(HAL_GetTick(), g_p0.boot_tick) < P0_UART_BOOT_QUIET_MS)
    {
        return 0U;
    }

    if ((P0_REQUIRE_BT != 0U) && (P0_IsBluetoothConnected() == 0U))
    {
        return 0U;
    }

    return 1U;
}

static void P0_UartTx_StartNext(void)
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
    g_uart_tx_tail = (uint16_t)((g_uart_tx_tail + 1U) % P0_UART_TX_BUFFER_SIZE);
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

static void P0_UartTx_ResetQueue(void)
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

static char P0_ToUpperChar(char c)
{
    if ((c >= 'a') && (c <= 'z'))
    {
        return (char)(c - 'a' + 'A');
    }

    return c;
}

static void P0_ToUpperString(char *dst, size_t dst_len, const char *src)
{
    size_t i;

    if ((dst == NULL) || (dst_len == 0U))
    {
        return;
    }

    for (i = 0U; (i < (dst_len - 1U)) && (src != NULL) && (src[i] != '\0'); i++)
    {
        dst[i] = P0_ToUpperChar(src[i]);
    }
    dst[i] = '\0';
}

static char *P0_NextToken(char **cursor)
{
    char *token;
    char *p;

    if ((cursor == NULL) || (*cursor == NULL))
    {
        return NULL;
    }

    p = *cursor;
    while ((*p == ' ') || (*p == '\t'))
    {
        p++;
    }

    if (*p == '\0')
    {
        *cursor = p;
        return NULL;
    }

    token = p;
    while ((*p != '\0') && (*p != ' ') && (*p != '\t'))
    {
        p++;
    }

    if (*p != '\0')
    {
        *p = '\0';
        p++;
    }
    *cursor = p;
    return token;
}

static uint8_t P0_ParseU16(const char *text, uint16_t *out)
{
    char *end_ptr;
    unsigned long value;

    if ((text == NULL) || (out == NULL))
    {
        return 0U;
    }

    value = strtoul(text, &end_ptr, 10);
    if ((end_ptr == text) || (*end_ptr != '\0') || (value > 65535UL))
    {
        return 0U;
    }

    *out = (uint16_t)value;
    return 1U;
}

#if (P0_ENABLE_UART_RPM_INPUT != 0U)
static uint8_t P0_ParseU32(const char *text, uint32_t *out)
{
    char *end_ptr;
    unsigned long value;

    if ((text == NULL) || (out == NULL))
    {
        return 0U;
    }

    value = strtoul(text, &end_ptr, 10);
    if ((end_ptr == text) || (*end_ptr != '\0'))
    {
        return 0U;
    }

    *out = (uint32_t)value;
    return 1U;
}
#endif

static void P0_SendBootBannerOnce(void)
{
    static const char banner[] =
        "# boot app=P0_RPM_MAPPING version=" P0_APP_VERSION " uart=9600 protocol=firewater\r\n";

    if (g_p0.boot_banner_sent == 0U)
    {
        if (P0_UartWrite(banner) != 0U)
        {
            g_p0.boot_banner_sent = 1U;
        }
    }
}

static void P0_SendConnectedBannerOnce(void)
{
    if (g_p0.connected_banner_sent == 0U)
    {
        if (P0_UartWrite("# connected\r\n") != 0U)
        {
            g_p0.connected_banner_sent = 1U;
        }
    }
}

static void P0_SendReadyBannerOnce(void)
{
    char line[128];
    int len;

    if (g_p0.ready_banner_sent != 0U)
    {
        return;
    }

    len = snprintf(line,
                   sizeof(line),
                   "# ready board=%s load=%s sweep_count=%u\r\n",
                   P0_BoardName(),
                   P0_LoadName(),
                   (unsigned int)g_p0.sweep_count);
    if (len > 0)
    {
        if (P0_UartWrite(line) != 0U)
        {
            g_p0.ready_banner_sent = 1U;
        }
    }
}

static void P0_SendHelp(void)
{
#if (P0_ENABLE_UART_RPM_INPUT != 0U)
    (void)P0_UartWrite(
        "# help commands=HELP,STATUS,BOARD Si,BOARD GaN,LOAD prop,LOAD noProp,SWEEP start end step,EXTEND ON,EXTEND OFF,CMD value,START,STOP,ABORT,RPM value,CLEAR_RPM,NEXT,REPEAT\r\n");
#else
    (void)P0_UartWrite(
        "# help commands=HELP,STATUS,BOARD Si,BOARD GaN,LOAD prop,LOAD noProp,SWEEP start end step,EXTEND ON,EXTEND OFF,CMD value,START,STOP,ABORT,NEXT,REPEAT\r\n");
#endif
}

static void P0_SendStatus(void)
{
    char line[224];
    char vbat_str[20];
    char current_str[20];
    char power_str[20];
    int len;

    P0_FormatFloat3(vbat_str, sizeof(vbat_str), g_p0.adc.vbat_V);
    P0_FormatFloat3(current_str, sizeof(current_str), g_p0.adc.current_A);
    P0_FormatFloat3(power_str, sizeof(power_str), g_p0.adc.power_W);
    len = snprintf(line,
                   sizeof(line),
                   "# status state=%s board=%s load=%s index=%u cmd=%u rpm_count=%u vbat=%s current=%s power=%s\r\n",
                   P0_RpmMap_GetStateName(g_p0.state),
                   P0_BoardName(),
                   P0_LoadName(),
                   (unsigned int)g_p0.step_index,
                   (unsigned int)g_p0.target_dshot_value,
                   (unsigned int)g_p0.rpm_count,
                   vbat_str,
                   current_str,
                   power_str);
    if (len > 0)
    {
        (void)P0_UartWrite(line);
    }
}

static void P0_SendCsvHeaderOnce(void)
{
    static const char header[] =
        "# firewater_channels,ch0=t_ms,ch1=state,ch2=step_index,ch3=cmd_raw,ch4=dshot_cmd,ch5=adc_i_raw,ch6=adc_vbat_raw,ch7=v_i_sense,ch8=v_vbat_adc,ch9=current_A,ch10=vbat_V,ch11=power_W,ch12=zero_offset_V,ch13=active_zero_offset_V,ch14=delta_i_V,ch15=rpm_count,ch16=rpm1,ch17=rpm2,ch18=rpm3,ch19=rpm_used,ch20=flags\r\n";

    if (g_p0.csv_header_sent == 0U)
    {
        if (P0_UartWrite(header) != 0U)
        {
            g_p0.csv_header_sent = 1U;
        }
    }
}

static void P0_SendCsvLine(const P0_AdcProcessed_t *adc_data)
{
    char line[384];
    char v_i_sense_str[20];
    char v_vbat_adc_str[20];
    char current_a_str[20];
    char vbat_v_str[20];
    char power_w_str[20];
    char zero_offset_str[20];
    char active_zero_offset_str[20];
    char delta_i_v_str[20];
    uint32_t rpm1 = 0U;
    uint32_t rpm2 = 0U;
    uint32_t rpm3 = 0U;
    uint32_t flags = 0U;
    uint32_t t_ms;
    int len;

    if (adc_data == NULL)
    {
        return;
    }

    P0_SendCsvHeaderOnce();
    if (g_p0.rpm_count > 0U)
    {
        rpm1 = g_p0.rpm_readings[0];
    }
    if (g_p0.rpm_count > 1U)
    {
        rpm2 = g_p0.rpm_readings[1];
    }
    if (g_p0.rpm_count > 2U)
    {
        rpm3 = g_p0.rpm_readings[2];
    }
    if (g_p0.board == P0_BOARD_UNKNOWN)
    {
        flags |= 0x01UL;
    }
    if (g_p0.load == P0_LOAD_UNKNOWN)
    {
        flags |= 0x02UL;
    }
#if (P0_ENABLE_UART_RPM_INPUT != 0U)
    if (g_p0.rpm_count < P0_RPM_REQUIRED_READINGS)
    {
        flags |= 0x04UL;
    }
#endif
    if (g_p0.state == P0_STATE_FAULT)
    {
        flags |= 0x08UL;
    }

    t_ms = P0_ElapsedMs(HAL_GetTick(), g_p0.session_start_tick);
    P0_FormatFloat3(v_i_sense_str, sizeof(v_i_sense_str), adc_data->v_i_sense);
    P0_FormatFloat3(v_vbat_adc_str, sizeof(v_vbat_adc_str), adc_data->v_vbat_adc);
    P0_FormatFloat3(current_a_str, sizeof(current_a_str), adc_data->current_A);
    P0_FormatFloat3(vbat_v_str, sizeof(vbat_v_str), adc_data->vbat_V);
    P0_FormatFloat3(power_w_str, sizeof(power_w_str), adc_data->power_W);
    P0_FormatFloat3(zero_offset_str, sizeof(zero_offset_str), g_p0.baseline_zero_offset_voltage);
    P0_FormatFloat3(active_zero_offset_str, sizeof(active_zero_offset_str), adc_data->active_zero_offset_V);
    P0_FormatFloat3(delta_i_v_str, sizeof(delta_i_v_str), adc_data->delta_i_V);

    len = snprintf(line,
                   sizeof(line),
                   "p0:%lu,%u,%u,%u,%u,%u,%u,%s,%s,%s,%s,%s,%s,%s,%s,%u,%lu,%lu,%lu,%lu,%lu\r\n",
                   (unsigned long)t_ms,
                   (unsigned int)g_p0.state,
                   (unsigned int)g_p0.step_index,
                   (unsigned int)g_p0.target_dshot_value,
                   (unsigned int)g_p0.current_dshot_value,
                   (unsigned int)adc_data->adc_i_raw,
                   (unsigned int)adc_data->adc_vbat_raw,
                   v_i_sense_str,
                   v_vbat_adc_str,
                   current_a_str,
                   vbat_v_str,
                   power_w_str,
                   zero_offset_str,
                   active_zero_offset_str,
                   delta_i_v_str,
                   (unsigned int)g_p0.rpm_count,
                   (unsigned long)rpm1,
                   (unsigned long)rpm2,
                   (unsigned long)rpm3,
                   (unsigned long)P0_RpmMedian(),
                   (unsigned long)flags);
    if (len > 0)
    {
        (void)P0_UartWrite(line);
    }
}

static void P0_SendSweepSetLine(void)
{
    char line[128];
    int len;

    len = snprintf(line,
                   sizeof(line),
                   "# sweep_set start=%u end=%u step=%u count=%u\r\n",
                   (unsigned int)g_p0.sweep[0],
                   (unsigned int)g_p0.sweep[g_p0.sweep_count - 1U],
                   (g_p0.sweep_count > 1U) ? (unsigned int)(g_p0.sweep[1] - g_p0.sweep[0]) : 0U,
                   (unsigned int)g_p0.sweep_count);
    if (len > 0)
    {
        (void)P0_UartWrite(line);
    }
}

#if (P0_ENABLE_UART_RPM_INPUT != 0U)
static void P0_SendRpmAddedLine(uint32_t rpm)
{
    char line[160];
    int len;

    if (g_p0.rpm_count >= P0_RPM_REQUIRED_READINGS)
    {
        len = snprintf(line,
                       sizeof(line),
                       "# rpm_added cmd=%u value=%lu count=%u rpm_used=%lu\r\n",
                       (unsigned int)g_p0.target_dshot_value,
                       (unsigned long)rpm,
                       (unsigned int)g_p0.rpm_count,
                       (unsigned long)P0_RpmMedian());
    }
    else
    {
        len = snprintf(line,
                       sizeof(line),
                       "# rpm_added cmd=%u value=%lu count=%u\r\n",
                       (unsigned int)g_p0.target_dshot_value,
                       (unsigned long)rpm,
                       (unsigned int)g_p0.rpm_count);
    }

    if (len > 0)
    {
        (void)P0_UartWrite(line);
    }
}
#endif

static void P0_SendStepSummary(const char *status)
{
    char line[512];
    char rpm_mean_str[20];
    char vbat_mean_str[20];
    char vbat_std_str[20];
    char current_mean_str[20];
    char current_std_str[20];
    char power_mean_str[20];
    char power_std_str[20];
    float vbat_mean;
    float current_mean;
    float power_mean;
    uint32_t rpm1 = 0U;
    uint32_t rpm2 = 0U;
    uint32_t rpm3 = 0U;
    uint32_t rpm_min = 0U;
    uint32_t rpm_max = 0U;
    int len;

    if (g_p0.rpm_count > 0U)
    {
        rpm1 = g_p0.rpm_readings[0];
        rpm_min = P0_RpmMin();
        rpm_max = P0_RpmMax();
    }
    if (g_p0.rpm_count > 1U)
    {
        rpm2 = g_p0.rpm_readings[1];
    }
    if (g_p0.rpm_count > 2U)
    {
        rpm3 = g_p0.rpm_readings[2];
    }

    vbat_mean = P0_StatsMean(g_p0.stats.sum_vbat, g_p0.stats.count);
    current_mean = P0_StatsMean(g_p0.stats.sum_current, g_p0.stats.count);
    power_mean = P0_StatsMean(g_p0.stats.sum_power, g_p0.stats.count);
    P0_FormatFloat1(rpm_mean_str, sizeof(rpm_mean_str), P0_RpmMean());
    P0_FormatFloat3(vbat_mean_str, sizeof(vbat_mean_str), vbat_mean);
    P0_FormatFloat3(vbat_std_str, sizeof(vbat_std_str), P0_StatsStd(g_p0.stats.sum_vbat, g_p0.stats.sum_vbat2, g_p0.stats.count));
    P0_FormatFloat3(current_mean_str, sizeof(current_mean_str), current_mean);
    P0_FormatFloat3(current_std_str, sizeof(current_std_str), P0_StatsStd(g_p0.stats.sum_current, g_p0.stats.sum_current2, g_p0.stats.count));
    P0_FormatFloat3(power_mean_str, sizeof(power_mean_str), power_mean);
    P0_FormatFloat3(power_std_str, sizeof(power_std_str), P0_StatsStd(g_p0.stats.sum_power, g_p0.stats.sum_power2, g_p0.stats.count));

    len = snprintf(line,
                   sizeof(line),
                   "# p0_step_summary date=NA board=%s load_mode=%s index=%u step_index=%u cmd=%u dshot_cmd=%u rpm1=%lu rpm2=%lu rpm3=%lu rpm_used=%lu rpm_mean=%s rpm_min=%lu rpm_max=%lu rpm_range=%lu vbat_mean_V=%s vbat_std_V=%s current_mean_A=%s current_std_A=%s power_mean_W=%s power_std_W=%s sample_count=%lu status=%s\r\n",
                   P0_BoardName(),
                   P0_LoadName(),
                   (unsigned int)g_p0.step_index,
                   (unsigned int)g_p0.step_index,
                   (unsigned int)g_p0.target_dshot_value,
                   (unsigned int)g_p0.target_dshot_value,
                   (unsigned long)rpm1,
                   (unsigned long)rpm2,
                   (unsigned long)rpm3,
                   (unsigned long)P0_RpmMedian(),
                   rpm_mean_str,
                   (unsigned long)rpm_min,
                   (unsigned long)rpm_max,
                   (unsigned long)((rpm_max >= rpm_min) ? (rpm_max - rpm_min) : 0U),
                   vbat_mean_str,
                   vbat_std_str,
                   current_mean_str,
                   current_std_str,
                   power_mean_str,
                   power_std_str,
                   (unsigned long)g_p0.stats.count,
                   (status != NULL) ? status : "unknown");
    if (len > 0)
    {
        (void)P0_UartWrite(line);
    }
}

static const char *P0_CurrentStepSummaryStatus(void)
{
#if (P0_ENABLE_UART_RPM_INPUT != 0U)
    return (g_p0.rpm_count >= P0_RPM_REQUIRED_READINGS) ? "ok" : "incomplete_rpm";
#else
    return "throttle_only";
#endif
}

static const char *P0_BoardName(void)
{
    switch (g_p0.board)
    {
    case P0_BOARD_SI:
        return "Si";
    case P0_BOARD_GAN:
        return "GaN";
    case P0_BOARD_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

static const char *P0_LoadName(void)
{
    switch (g_p0.load)
    {
    case P0_LOAD_PROP:
        return "prop";
    case P0_LOAD_NOPROP:
        return "noProp";
    case P0_LOAD_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}

static uint32_t P0_RpmMedian(void)
{
    uint32_t sorted[P0_RPM_MAX_READINGS];
    uint8_t i;
    uint8_t j;
    uint8_t count = g_p0.rpm_count;

    if (count == 0U)
    {
        return 0U;
    }

    for (i = 0U; i < count; i++)
    {
        sorted[i] = g_p0.rpm_readings[i];
    }

    for (i = 0U; i < count; i++)
    {
        for (j = (uint8_t)(i + 1U); j < count; j++)
        {
            if (sorted[j] < sorted[i])
            {
                uint32_t tmp = sorted[i];
                sorted[i] = sorted[j];
                sorted[j] = tmp;
            }
        }
    }

    if ((count & 1U) == 0U)
    {
        return (uint32_t)((sorted[(count / 2U) - 1U] + sorted[count / 2U]) / 2UL);
    }

    return sorted[count / 2U];
}

static float P0_RpmMean(void)
{
    uint8_t i;
    uint32_t sum = 0U;

    if (g_p0.rpm_count == 0U)
    {
        return 0.0f;
    }

    for (i = 0U; i < g_p0.rpm_count; i++)
    {
        sum += g_p0.rpm_readings[i];
    }

    return (float)sum / (float)g_p0.rpm_count;
}

static uint32_t P0_RpmMin(void)
{
    uint8_t i;
    uint32_t value;

    if (g_p0.rpm_count == 0U)
    {
        return 0U;
    }

    value = g_p0.rpm_readings[0];
    for (i = 1U; i < g_p0.rpm_count; i++)
    {
        if (g_p0.rpm_readings[i] < value)
        {
            value = g_p0.rpm_readings[i];
        }
    }

    return value;
}

static uint32_t P0_RpmMax(void)
{
    uint8_t i;
    uint32_t value;

    if (g_p0.rpm_count == 0U)
    {
        return 0U;
    }

    value = g_p0.rpm_readings[0];
    for (i = 1U; i < g_p0.rpm_count; i++)
    {
        if (g_p0.rpm_readings[i] > value)
        {
            value = g_p0.rpm_readings[i];
        }
    }

    return value;
}

static void P0_ClearRpmReadings(void)
{
    memset(g_p0.rpm_readings, 0, sizeof(g_p0.rpm_readings));
    g_p0.rpm_count = 0U;
}

static void P0_FormatFloat(char *dst, size_t len, float value, uint8_t decimals)
{
    int32_t scaled;
    int32_t abs_scaled;
    int32_t integer_part;
    int32_t fractional_part;
    int32_t scale = 1;
    uint8_t i;
    const char *sign_str;

    if ((dst == NULL) || (len == 0U))
    {
        return;
    }

    for (i = 0U; i < decimals; i++)
    {
        scale *= 10;
    }

    scaled = (int32_t)(value * (float)scale + ((value >= 0.0f) ? 0.5f : -0.5f));
    sign_str = (scaled < 0) ? "-" : "";
    abs_scaled = (scaled < 0) ? -scaled : scaled;
    integer_part = abs_scaled / scale;
    fractional_part = abs_scaled % scale;

    if (decimals == 0U)
    {
        (void)snprintf(dst, len, "%s%ld", sign_str, (long)integer_part);
    }
    else
    {
        (void)snprintf(dst,
                       len,
                       "%s%ld.%0*ld",
                       sign_str,
                       (long)integer_part,
                       (int)decimals,
                       (long)fractional_part);
    }
}

static void P0_FormatFloat3(char *dst, size_t len, float value)
{
    P0_FormatFloat(dst, len, value, 3U);
}

static void P0_FormatFloat1(char *dst, size_t len, float value)
{
    P0_FormatFloat(dst, len, value, 1U);
}

static HAL_StatusTypeDef P0_Oled_WriteCommand(uint8_t command)
{
    uint8_t frame[2];

    if (g_p0.oled_ready == 0U)
    {
        return HAL_ERROR;
    }

    frame[0] = 0x00U;
    frame[1] = command;
    return HAL_I2C_Master_Transmit(&hi2c1, P0_OLED_I2C_ADDR, frame, 2U, P0_OLED_I2C_TIMEOUT_MS);
}

static HAL_StatusTypeDef P0_Oled_WriteData(const uint8_t *data, uint16_t size)
{
    uint8_t frame[129];

    if ((g_p0.oled_ready == 0U) || (data == NULL) || (size > 128U))
    {
        return HAL_ERROR;
    }

    frame[0] = 0x40U;
    memcpy(&frame[1], data, size);
    return HAL_I2C_Master_Transmit(&hi2c1, P0_OLED_I2C_ADDR, frame, (uint16_t)(size + 1U), P0_OLED_I2C_TIMEOUT_MS);
}

static void P0_Oled_Init(void)
{
    static const uint8_t init_cmds[] =
    {
        0xAEU, 0x20U, 0x02U, 0xB0U, 0xC8U, 0x00U, 0x10U, 0x40U,
        0x81U, 0x7FU, 0xA1U, 0xA6U, 0xA8U, 0x3FU, 0xA4U, 0xD3U,
        0x00U, 0xD5U, 0x80U, 0xD9U, 0xF1U, 0xDAU, 0x12U, 0xDBU,
        0x20U, 0x8DU, 0x14U, 0xAFU
    };
    uint8_t i;

    g_p0.oled_ready = 0U;
    if (HAL_I2C_IsDeviceReady(&hi2c1, P0_OLED_I2C_ADDR, 2U, 100U) != HAL_OK)
    {
        return;
    }

    g_p0.oled_ready = 1U;
    for (i = 0U; i < (uint8_t)(sizeof(init_cmds) / sizeof(init_cmds[0])); i++)
    {
        if (P0_Oled_WriteCommand(init_cmds[i]) != HAL_OK)
        {
            g_p0.oled_ready = 0U;
            return;
        }
    }

    P0_Oled_Clear();
}

static void P0_Oled_Service(uint32_t now_ms)
{
#if (P0_OLED_UPDATE_WHILE_ACTIVE == 0U)
    if (P0_IsActiveState() != 0U)
    {
        return;
    }
#endif

    if (g_p0.oled_ready != 0U)
    {
        return;
    }

    if (g_p0.last_oled_init_attempt_tick == 0U)
    {
        if (now_ms < P0_OLED_POWERUP_DELAY_MS)
        {
            return;
        }
    }
    else if (P0_ElapsedMs(now_ms, g_p0.last_oled_init_attempt_tick) < P0_OLED_RETRY_INTERVAL_MS)
    {
        return;
    }

    g_p0.last_oled_init_attempt_tick = now_ms;
    P0_Oled_Init();
    if (g_p0.oled_ready != 0U)
    {
        g_p0.last_oled_update_tick = 0U;
        P0_Oled_Update(now_ms);
    }
}

static void P0_Oled_Clear(void)
{
    if (g_p0.oled_ready == 0U)
    {
        return;
    }

    memset(g_oled_buffer, 0, sizeof(g_oled_buffer));
    P0_Oled_Flush();
}

static void P0_Oled_Flush(void)
{
    uint8_t page;

    if (g_p0.oled_ready == 0U)
    {
        return;
    }

    for (page = 0U; page < 8U; page++)
    {
        if ((P0_Oled_WriteCommand((uint8_t)(0xB0U | page)) != HAL_OK) ||
            (P0_Oled_WriteCommand((uint8_t)(0x00U | (P0_OLED_COLUMN_OFFSET & 0x0FU))) != HAL_OK) ||
            (P0_Oled_WriteCommand((uint8_t)(0x10U | ((P0_OLED_COLUMN_OFFSET >> 4U) & 0x0FU))) != HAL_OK) ||
            (P0_Oled_WriteData(&g_oled_buffer[page * 128U], 128U) != HAL_OK))
        {
            g_p0.oled_ready = 0U;
            g_p0.last_oled_init_attempt_tick = HAL_GetTick();
            return;
        }
    }
}

static void P0_Oled_Update(uint32_t now_ms)
{
    char line0[32];
    char line1[32];
    char line2[32];
    char line3[32];
    char line4[32];
    char value_str[20];
    uint32_t elapsed_ms;
    uint32_t remaining_s;

    if (g_p0.oled_ready == 0U)
    {
        return;
    }

#if (P0_OLED_UPDATE_WHILE_ACTIVE == 0U)
    if (P0_IsActiveState() != 0U)
    {
        return;
    }
#endif

    if ((g_p0.last_oled_update_tick != 0U) &&
        (P0_ElapsedMs(now_ms, g_p0.last_oled_update_tick) < P0_OLED_UPDATE_INTERVAL_MS))
    {
        return;
    }

    g_p0.last_oled_update_tick = now_ms;
    memset(g_oled_buffer, 0, sizeof(g_oled_buffer));

    P0_FormatFloat3(value_str, sizeof(value_str), g_p0.adc.vbat_V);
    (void)snprintf(line0, sizeof(line0), "VBAT %sV", value_str);
    P0_FormatFloat3(value_str, sizeof(value_str), g_p0.adc.current_A);
    (void)snprintf(line1, sizeof(line1), "CURR %sA", value_str);
    P0_FormatFloat3(value_str, sizeof(value_str), g_p0.adc.power_W);
    (void)snprintf(line2, sizeof(line2), "POWR %sW", value_str);

    if ((g_p0.state == P0_STATE_READY) ||
        (g_p0.state == P0_STATE_DONE) ||
        (g_p0.state == P0_STATE_ABORTED))
    {
        (void)snprintf(line3, sizeof(line3), "P0 CMD%04u", (unsigned int)g_p0.preview_dshot_value);
    }
    else
    {
        (void)snprintf(line3, sizeof(line3), "P0 CMD%04u", (unsigned int)g_p0.target_dshot_value);
    }

    elapsed_ms = P0_ElapsedMs(now_ms, g_p0.state_enter_tick);
    remaining_s = 0U;
    switch (g_p0.state)
    {
    case P0_STATE_WAIT_BT:
        (void)snprintf(line4, sizeof(line4), "WAIT BT");
        break;
    case P0_STATE_READY:
        (void)snprintf(line4, sizeof(line4), "READY");
        break;
    case P0_STATE_PREPARE:
        remaining_s = (elapsed_ms >= P0_PREPARE_MS) ? 0U : ((P0_PREPARE_MS - elapsed_ms + 999U) / 1000U);
        (void)snprintf(line4, sizeof(line4), "PREP %02luS", (unsigned long)remaining_s);
        break;
    case P0_STATE_RAMP:
        remaining_s = (elapsed_ms >= g_p0.ramp_duration_ms) ? 0U : ((g_p0.ramp_duration_ms - elapsed_ms + 999U) / 1000U);
        (void)snprintf(line4, sizeof(line4), "RAMP %02luS", (unsigned long)remaining_s);
        break;
    case P0_STATE_STABILIZE:
        remaining_s = (elapsed_ms >= P0_STABILIZE_MS) ? 0U : ((P0_STABILIZE_MS - elapsed_ms + 999U) / 1000U);
        (void)snprintf(line4, sizeof(line4), "STAB %02luS", (unsigned long)remaining_s);
        break;
    case P0_STATE_MEASURE:
#if (P0_ENABLE_UART_RPM_INPUT != 0U)
        (void)snprintf(line4, sizeof(line4), "RPM %u/%u", (unsigned int)g_p0.rpm_count, (unsigned int)P0_RPM_REQUIRED_READINGS);
#else
        (void)snprintf(line4, sizeof(line4), "HOLD NEXT");
#endif
        break;
    case P0_STATE_REST:
        remaining_s = (elapsed_ms >= P0_REST_MS) ? 0U : ((P0_REST_MS - elapsed_ms + 999U) / 1000U);
        (void)snprintf(line4, sizeof(line4), "REST %02luS", (unsigned long)remaining_s);
        break;
    case P0_STATE_DONE:
        (void)snprintf(line4, sizeof(line4), "DONE");
        break;
    case P0_STATE_ABORTED:
        (void)snprintf(line4, sizeof(line4), "ABORT");
        break;
    case P0_STATE_FAULT:
        (void)snprintf(line4, sizeof(line4), "FAULT OC");
        break;
    default:
        (void)snprintf(line4, sizeof(line4), "FAULT");
        break;
    }

    P0_Oled_DrawLine(0U, line0);
    P0_Oled_DrawLine(1U, line1);
    P0_Oled_DrawLine(2U, line2);
    P0_Oled_DrawLine(3U, line3);
    P0_Oled_DrawLine(4U, line4);
    P0_Oled_Flush();
}

static void P0_Oled_DrawLine(uint8_t line_index, const char *text)
{
    uint8_t x = 0U;
    uint8_t y;

    if ((g_p0.oled_ready == 0U) || (line_index >= 5U))
    {
        return;
    }

    y = (uint8_t)(line_index * 11U);
    while ((text != NULL) && (*text != '\0') && (x <= 120U))
    {
        P0_Oled_DrawChar(x, y, *text);
        x = (uint8_t)(x + 7U);
        text++;
    }
}

static void P0_Oled_DrawChar(uint8_t x, uint8_t y, char c)
{
    uint8_t glyph[5];
    uint8_t col;
    uint8_t row;

    P0_Oled_GetGlyph5x7(P0_ToUpperChar(c), glyph);
    for (col = 0U; col < 5U; col++)
    {
        for (row = 0U; row < 7U; row++)
        {
            if ((glyph[col] & (uint8_t)(1U << row)) != 0U)
            {
                P0_Oled_SetPixel((uint8_t)(x + col), (uint8_t)(y + row), 1U);
                P0_Oled_SetPixel((uint8_t)(x + col + 1U), (uint8_t)(y + row), 1U);
            }
        }
    }
}

static void P0_Oled_SetPixel(uint8_t x, uint8_t y, uint8_t on)
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

static void P0_Oled_GetGlyph5x7(char c, uint8_t glyph[5])
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
    case '/': glyph[0] = 0x40U; glyph[1] = 0x30U; glyph[2] = 0x08U; glyph[3] = 0x06U; glyph[4] = 0x01U; break;
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
    case 'K': glyph[0] = 0x7FU; glyph[1] = 0x08U; glyph[2] = 0x14U; glyph[3] = 0x22U; glyph[4] = 0x41U; break;
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
    case 'X': glyph[0] = 0x63U; glyph[1] = 0x14U; glyph[2] = 0x08U; glyph[3] = 0x14U; glyph[4] = 0x63U; break;
    case 'Y': glyph[0] = 0x07U; glyph[1] = 0x08U; glyph[2] = 0x70U; glyph[3] = 0x08U; glyph[4] = 0x07U; break;
    default: break;
    }
}

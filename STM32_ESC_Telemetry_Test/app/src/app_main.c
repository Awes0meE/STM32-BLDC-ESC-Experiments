#include "app_main.h"

#include <stdio.h>
#include <string.h>

#include "adc.h"
#include "app_arm.h"
#include "app_config.h"
#include "app_display.h"
#include "app_motor.h"
#include "app_rc_crsf.h"
#include "bsp_adc_mon.h"
#include "bsp_dshot.h"
#include "bsp_hc05.h"
#include "bsp_key.h"
#include "usart.h"

typedef struct
{
    uint32_t last_status_ms;
    uint16_t uart2_dma_last_pos;
} AppMainContext_t;

#define APP_USART2_DMA_RX_BUFFER_SIZE  2048U

static AppMainContext_t g_app;
static uint8_t g_uart1_rx_byte;
static uint8_t g_uart2_dma_rx_buffer[APP_USART2_DMA_RX_BUFFER_SIZE];

static uint16_t App_Main_GetChannelUs(const AppRcCrsfData_t *rc_data, uint8_t channel_1based);
static void App_Main_FormatFixed2(char *dst, size_t len, float value);
static void App_Main_SendStatusLine(uint8_t rx_active,
                                    const AppRcCrsfData_t *rc_data,
                                    const AppArmStatus_t *arm_status,
                                    const AppMotorStatus_t *motor_status,
                                    uint16_t throttle_us,
                                    uint16_t arm_channel_us,
                                    uint16_t dshot_command,
                                    const BspAdcMonData_t *adc_data);
static void App_Main_StartUartReception(void);
static void App_Main_StartUsart2DmaReception(void);
static void App_Main_ServiceUsart2DmaRx(void);

void App_Main_Init(void)
{
    memset(&g_app, 0, sizeof(g_app));

    Bsp_Hc05_Init();
    Bsp_Key_Init();
    Bsp_Dshot_Init();
    Bsp_AdcMon_Init();
    App_RcCrsf_Init();
    App_Arm_Init();
    App_Motor_Init();
    App_Display_Init();
    App_Main_StartUartReception();

    Bsp_Hc05_Write("# boot,fw=ELRS_DSHOT_BENCH,rc=CRSF420000,dshot=PB8,uart1=HC05\r\n");
}

void App_Main_Task(void)
{
    AppRcCrsfData_t rc_data;
    AppArmInputs_t arm_inputs;
    AppArmStatus_t arm_status;
    AppMotorStatus_t motor_status;
    AppDisplayModel_t display_model;
    const BspAdcMonData_t *adc_data;
    uint32_t now_ms;
    uint16_t throttle_us;
    uint16_t arm_channel_us;
    uint16_t dshot_command;
    uint8_t rx_active;
    uint8_t hc05_stop_request;
    uint8_t hc05_status_request;

    App_Main_ServiceUsart2DmaRx();
    now_ms = HAL_GetTick();
    Bsp_Key_Task(now_ms);
    App_RcCrsf_GetSnapshot(&rc_data);

    rx_active = App_RcCrsf_HasFreshFrame(now_ms, APP_RX_FAILSAFE_TIMEOUT_MS);
    throttle_us = rx_active != 0U ?
        App_Main_GetChannelUs(&rc_data, APP_RC_THROTTLE_CH) : APP_RC_MIN_COMMAND_US;
    arm_channel_us = rx_active != 0U ?
        App_Main_GetChannelUs(&rc_data, APP_RC_ARM_CH) : APP_RC_MIN_COMMAND_US;

    hc05_stop_request = Bsp_Hc05_ConsumeStopRequest();
    hc05_status_request = Bsp_Hc05_ConsumeStatusRequest();

    arm_inputs.rx_active = rx_active;
    arm_inputs.throttle_us = throttle_us;
    arm_inputs.arm_channel_us = arm_channel_us;
    arm_inputs.toggle_estop_request = Bsp_Key_ConsumeShortPress();
    arm_inputs.force_estop_request = hc05_stop_request;

    App_Arm_Update(&arm_inputs, now_ms);
    App_Arm_GetStatus(&arm_status);

    Bsp_AdcMon_Task((Bsp_Dshot_GetThrottle() == 0U) ? 1U : 0U, now_ms);
    adc_data = Bsp_AdcMon_GetData();

    dshot_command = App_Motor_GetOutputCommand(arm_status.armed, throttle_us, adc_data, now_ms);
    App_Motor_GetStatus(&motor_status);
    Bsp_Dshot_SetThrottle(dshot_command);
    Bsp_Dshot_Task(now_ms);

    display_model.state = arm_status.state;
    display_model.rx_active = rx_active;
    display_model.armed = arm_status.armed;
    display_model.protect_active = motor_status.protect_active;
    display_model.protect_reason = motor_status.protect_reason;
    display_model.throttle_us = throttle_us;
    display_model.dshot_command = dshot_command;
    display_model.vbat_v = adc_data->vbat_v;
    display_model.current_a = adc_data->current_a;
    App_Display_Task(&display_model, now_ms);

    if (((now_ms - g_app.last_status_ms) >= APP_HC05_STATUS_INTERVAL_MS) ||
        (hc05_status_request != 0U))
    {
        g_app.last_status_ms = now_ms;
        App_Main_SendStatusLine(rx_active, &rc_data, &arm_status, &motor_status, throttle_us, arm_channel_us, dshot_command, adc_data);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
    {
        return;
    }

    if (huart->Instance == USART1)
    {
        Bsp_Hc05_HandleRxByte(g_uart1_rx_byte);
        (void)HAL_UART_Receive_IT(&huart1, &g_uart1_rx_byte, 1U);
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if ((huart != NULL) && (huart->Instance == USART1))
    {
        Bsp_Hc05_HandleTxComplete();
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == NULL)
    {
        return;
    }

    if (huart->Instance == USART1)
    {
        (void)HAL_UART_Receive_IT(&huart1, &g_uart1_rx_byte, 1U);
    }
    else if (huart->Instance == USART2)
    {
        App_RcCrsf_ReportUartError(huart->ErrorCode);
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        App_Main_StartUsart2DmaReception();
    }
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
{
    Bsp_Dshot_HandlePulseFinished(htim);
}

static uint16_t App_Main_GetChannelUs(const AppRcCrsfData_t *rc_data, uint8_t channel_1based)
{
    uint8_t index;

    if ((rc_data == NULL) || (channel_1based == 0U) || (channel_1based > APP_RC_CHANNEL_COUNT))
    {
        return APP_RC_MIN_COMMAND_US;
    }

    index = (uint8_t)(channel_1based - 1U);
    return rc_data->us[index];
}

static void App_Main_FormatFixed2(char *dst, size_t len, float value)
{
    int32_t scaled;
    int32_t integer_part;
    int32_t fractional_part;

    if ((dst == NULL) || (len == 0U))
    {
        return;
    }

    scaled = (int32_t)(value * 100.0f + 0.5f);
    if (value < 0.0f)
    {
        scaled = (int32_t)(value * 100.0f - 0.5f);
    }
    integer_part = scaled / 100;
    fractional_part = scaled % 100;
    if (fractional_part < 0)
    {
        fractional_part = -fractional_part;
    }

    (void)snprintf(dst, len, "%ld.%02ld", (long)integer_part, (long)fractional_part);
}

static void App_Main_SendStatusLine(uint8_t rx_active,
                                    const AppRcCrsfData_t *rc_data,
                                    const AppArmStatus_t *arm_status,
                                    const AppMotorStatus_t *motor_status,
                                    uint16_t throttle_us,
                                    uint16_t arm_channel_us,
                                    uint16_t dshot_command,
                                    const BspAdcMonData_t *adc_data)
{
    char line[256];
    char vbat_text[16];
    char current_text[16];
    int length;

    if ((rc_data == NULL) || (arm_status == NULL) || (motor_status == NULL) || (adc_data == NULL))
    {
        return;
    }

    App_Main_FormatFixed2(vbat_text, sizeof(vbat_text), adc_data->vbat_v);
    App_Main_FormatFixed2(current_text, sizeof(current_text), adc_data->current_a);

    length = snprintf(line,
                      sizeof(line),
                      "# status,state=%s,rx=%u,link=%u,arm=%u,arm_sw=%u,tl=%u,seen=%u,estop=%u,drop=%s,thr_us=%u,arm_us=%u,dshot=%u,vbat=%s,current=%s,prot=%u,reason=%s,trip=%lu,bt=%u,vf=%lu,ce=%lu,se=%lu,ue=%lu\r\n",
                      App_Arm_GetStateName(arm_status->state),
                      (unsigned int)rx_active,
                      (unsigned int)arm_status->rx_link_established,
                      (unsigned int)arm_status->armed,
                      (unsigned int)arm_status->arm_switch_on,
                      (unsigned int)arm_status->throttle_low,
                      (unsigned int)arm_status->arm_switch_seen_off,
                      (unsigned int)arm_status->estop_latched,
                      App_Arm_GetDropReasonName(arm_status->drop_reason),
                      (unsigned int)throttle_us,
                      (unsigned int)arm_channel_us,
                      (unsigned int)dshot_command,
                      vbat_text,
                      current_text,
                      (unsigned int)motor_status->protect_active,
                      App_Motor_GetProtectReasonName(motor_status->protect_reason),
                      (unsigned long)motor_status->protect_trip_count,
                      (unsigned int)Bsp_Hc05_IsConnected(),
                      (unsigned long)rc_data->diag.valid_frame_count,
                      (unsigned long)rc_data->diag.crc_error_count,
                      (unsigned long)rc_data->diag.size_error_count,
                      (unsigned long)rc_data->diag.uart_error_count);
    if (length > 0)
    {
        Bsp_Hc05_Write(line);
    }
}

static void App_Main_StartUartReception(void)
{
    if (HAL_UART_Receive_IT(&huart1, &g_uart1_rx_byte, 1U) != HAL_OK)
    {
        Error_Handler();
    }

    App_Main_StartUsart2DmaReception();
}

static void App_Main_StartUsart2DmaReception(void)
{
    g_app.uart2_dma_last_pos = 0U;
    (void)HAL_UART_DMAStop(&huart2);

    if (HAL_UART_Receive_DMA(&huart2,
                             g_uart2_dma_rx_buffer,
                             (uint16_t)sizeof(g_uart2_dma_rx_buffer)) != HAL_OK)
    {
        Error_Handler();
    }

    if (huart2.hdmarx != NULL)
    {
        __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
        __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_TC);
    }
}

static void App_Main_ServiceUsart2DmaRx(void)
{
    uint16_t current_pos;

    if (huart2.hdmarx == NULL)
    {
        return;
    }

    current_pos = (uint16_t)(APP_USART2_DMA_RX_BUFFER_SIZE - __HAL_DMA_GET_COUNTER(huart2.hdmarx));
    if (current_pos >= APP_USART2_DMA_RX_BUFFER_SIZE)
    {
        current_pos = 0U;
    }

    while (g_app.uart2_dma_last_pos != current_pos)
    {
        App_RcCrsf_HandleRxByte(g_uart2_dma_rx_buffer[g_app.uart2_dma_last_pos]);
        g_app.uart2_dma_last_pos++;
        if (g_app.uart2_dma_last_pos >= APP_USART2_DMA_RX_BUFFER_SIZE)
        {
            g_app.uart2_dma_last_pos = 0U;
        }
    }
}

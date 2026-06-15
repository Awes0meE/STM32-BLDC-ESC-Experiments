#include "bsp_dshot.h"

#include <string.h>

#include "app_config.h"
#include "tim.h"

#define DSHOT_FRAME_BITS         16U
#define DSHOT_RESET_SLOTS        8U
#define DSHOT_DMA_BUFFER_LENGTH  (DSHOT_FRAME_BITS + DSHOT_RESET_SLOTS)
#define DSHOT_BIT_0_HIGH_TICKS   90U
#define DSHOT_BIT_1_HIGH_TICKS   180U

typedef struct
{
    uint16_t current_command;
    uint16_t dma_buffer[DSHOT_DMA_BUFFER_LENGTH];
    uint32_t last_send_tick;
    uint8_t dma_busy;
} BspDshotContext_t;

static BspDshotContext_t g_dshot;

static uint16_t Bsp_Dshot_ClampThrottle(uint16_t command);
static uint16_t Bsp_Dshot_BuildPacket(uint16_t throttle_value);
static void Bsp_Dshot_PrepareFrame(uint16_t throttle_value);
static void Bsp_Dshot_TriggerFrame(uint16_t throttle_value);

void Bsp_Dshot_Init(void)
{
    memset(&g_dshot, 0, sizeof(g_dshot));

    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);
    Bsp_Dshot_TriggerFrame(0U);
}

void Bsp_Dshot_SetThrottle(uint16_t command)
{
    g_dshot.current_command = Bsp_Dshot_ClampThrottle(command);
}

uint16_t Bsp_Dshot_GetThrottle(void)
{
    return g_dshot.current_command;
}

uint8_t Bsp_Dshot_IsBusy(void)
{
    return g_dshot.dma_busy;
}

void Bsp_Dshot_Task(uint32_t now_ms)
{
    if ((now_ms - g_dshot.last_send_tick) < APP_DSHOT_SEND_INTERVAL_MS)
    {
        return;
    }

    if (g_dshot.dma_busy != 0U)
    {
        return;
    }

    g_dshot.last_send_tick = now_ms;
    Bsp_Dshot_TriggerFrame(g_dshot.current_command);
}

void Bsp_Dshot_HandlePulseFinished(TIM_HandleTypeDef *htim)
{
    if ((htim == NULL) || (htim->Instance != TIM4))
    {
        return;
    }

    (void)HAL_TIM_PWM_Stop_DMA(htim, TIM_CHANNEL_3);
    __HAL_TIM_SET_COMPARE(htim, TIM_CHANNEL_3, 0U);
    __HAL_TIM_SET_COUNTER(htim, 0U);
    g_dshot.dma_busy = 0U;
}

static uint16_t Bsp_Dshot_ClampThrottle(uint16_t command)
{
    if (command == 0U)
    {
        return 0U;
    }

    if (command < APP_DSHOT_MIN_THROTTLE)
    {
        return APP_DSHOT_MIN_THROTTLE;
    }

    if (command > APP_DSHOT_MAX_THROTTLE)
    {
        return APP_DSHOT_MAX_THROTTLE;
    }

    return command;
}

static uint16_t Bsp_Dshot_BuildPacket(uint16_t throttle_value)
{
    uint16_t packet;
    uint16_t checksum;
    uint16_t csum_data;
    uint8_t i;

    packet = (uint16_t)((throttle_value << 1U) | (APP_DSHOT_TELEMETRY_BIT & 0x1U));
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

static void Bsp_Dshot_PrepareFrame(uint16_t throttle_value)
{
    uint16_t packet;
    uint8_t i;

    packet = Bsp_Dshot_BuildPacket(throttle_value);

    for (i = 0U; i < DSHOT_FRAME_BITS; i++)
    {
        g_dshot.dma_buffer[i] =
            ((packet & 0x8000U) != 0U) ? DSHOT_BIT_1_HIGH_TICKS : DSHOT_BIT_0_HIGH_TICKS;
        packet <<= 1U;
    }

    for (i = DSHOT_FRAME_BITS; i < DSHOT_DMA_BUFFER_LENGTH; i++)
    {
        g_dshot.dma_buffer[i] = 0U;
    }
}

static void Bsp_Dshot_TriggerFrame(uint16_t throttle_value)
{
    HAL_StatusTypeDef status;

    if (g_dshot.dma_busy != 0U)
    {
        return;
    }

    Bsp_Dshot_PrepareFrame(throttle_value);
    g_dshot.dma_busy = 1U;

    __HAL_TIM_DISABLE(&htim4);
    __HAL_TIM_SET_COUNTER(&htim4, 0U);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, g_dshot.dma_buffer[0]);

    status = HAL_TIM_PWM_Start_DMA(&htim4,
                                   TIM_CHANNEL_3,
                                   (uint32_t *)&g_dshot.dma_buffer[1],
                                   (uint16_t)(DSHOT_DMA_BUFFER_LENGTH - 1U));
    if (status != HAL_OK)
    {
        g_dshot.dma_busy = 0U;
        Error_Handler();
    }
}

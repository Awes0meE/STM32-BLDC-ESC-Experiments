#include "bsp_adc_mon.h"

#include <string.h>

#include "adc.h"
#include "app_config.h"

typedef struct
{
    volatile uint16_t dma_buffer[APP_ADC_DMA_BUFFER_LENGTH];
    BspAdcMonData_t data;
    uint32_t zero_offset_sample_count;
    uint32_t last_zero_offset_tick;
    uint32_t last_current_filter_tick;
    float zero_offset_sum_v;
    float baseline_zero_offset_v;
    float current_filter_sum_a;
    float current_filter_buffer[APP_CURRENT_FILTER_WINDOW_SAMPLES];
    uint8_t current_filter_index;
    uint8_t current_filter_count;
} BspAdcMonContext_t;

static BspAdcMonContext_t g_adc_mon;

static void Bsp_AdcMon_ProcessAverage(BspAdcMonData_t *out_data);
static void Bsp_AdcMon_UpdateZeroOffset(uint8_t motor_stopped, uint32_t now_ms);
static void Bsp_AdcMon_UpdateCurrentDerived(uint32_t now_ms);

void Bsp_AdcMon_Init(void)
{
    memset(&g_adc_mon, 0, sizeof(g_adc_mon));
    g_adc_mon.baseline_zero_offset_v = APP_CURRENT_OFFSET_V;
    g_adc_mon.data.active_zero_offset_v = APP_CURRENT_OFFSET_V;

    if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc_mon.dma_buffer, APP_ADC_DMA_BUFFER_LENGTH) != HAL_OK)
    {
        Error_Handler();
    }
}

void Bsp_AdcMon_Task(uint8_t motor_stopped, uint32_t now_ms)
{
    Bsp_AdcMon_ProcessAverage(&g_adc_mon.data);
    Bsp_AdcMon_UpdateZeroOffset(motor_stopped, now_ms);
    Bsp_AdcMon_UpdateCurrentDerived(now_ms);
}

const BspAdcMonData_t *Bsp_AdcMon_GetData(void)
{
    return &g_adc_mon.data;
}

static void Bsp_AdcMon_ProcessAverage(BspAdcMonData_t *out_data)
{
    uint32_t i;
    uint32_t sum_i = 0U;
    uint32_t sum_vbat = 0U;
    const float adc_to_volt = APP_ADC_REF_VOLTAGE / APP_ADC_MAX_COUNTS;

    if (out_data == NULL)
    {
        return;
    }

    for (i = 0U; i < APP_ADC_DMA_BUFFER_LENGTH; i += APP_ADC_CHANNEL_COUNT)
    {
        sum_i += g_adc_mon.dma_buffer[i];
        sum_vbat += g_adc_mon.dma_buffer[i + 1U];
    }

    out_data->adc_i_raw = (uint16_t)(sum_i / APP_ADC_DMA_SAMPLES_PER_CHANNEL);
    out_data->adc_vbat_raw = (uint16_t)(sum_vbat / APP_ADC_DMA_SAMPLES_PER_CHANNEL);

    out_data->v_i_sense = (float)out_data->adc_i_raw * adc_to_volt;
    out_data->v_vbat_adc = (float)out_data->adc_vbat_raw * adc_to_volt;
    out_data->vbat_v = out_data->v_vbat_adc * APP_VBAT_DIVIDER_RATIO;
}

static void Bsp_AdcMon_UpdateZeroOffset(uint8_t motor_stopped, uint32_t now_ms)
{
    if (motor_stopped == 0U)
    {
        return;
    }

    if ((now_ms - g_adc_mon.last_zero_offset_tick) < APP_ZERO_OFFSET_SAMPLE_INTERVAL_MS)
    {
        return;
    }

    g_adc_mon.last_zero_offset_tick = now_ms;

    if (g_adc_mon.zero_offset_sample_count < APP_BASELINE_SAMPLE_COUNT)
    {
        g_adc_mon.zero_offset_sum_v += g_adc_mon.data.v_i_sense;
        g_adc_mon.zero_offset_sample_count++;
        g_adc_mon.baseline_zero_offset_v =
            g_adc_mon.zero_offset_sum_v / (float)g_adc_mon.zero_offset_sample_count;
    }
    else
    {
        g_adc_mon.baseline_zero_offset_v =
            (g_adc_mon.baseline_zero_offset_v * 0.99f) + (g_adc_mon.data.v_i_sense * 0.01f);
    }

    g_adc_mon.data.active_zero_offset_v = g_adc_mon.baseline_zero_offset_v;
}

static void Bsp_AdcMon_UpdateCurrentDerived(uint32_t now_ms)
{
    float signed_delta_v;
    float instant_current_a;

    g_adc_mon.data.active_zero_offset_v = g_adc_mon.baseline_zero_offset_v;
    g_adc_mon.data.delta_i_v = g_adc_mon.data.v_i_sense - g_adc_mon.baseline_zero_offset_v;

#if (APP_CURRENT_SIGN_INVERT != 0U)
    signed_delta_v = -g_adc_mon.data.delta_i_v;
#else
    signed_delta_v = g_adc_mon.data.delta_i_v;
#endif

    instant_current_a = signed_delta_v * APP_CURRENT_SCALE_A_PER_V;
    g_adc_mon.data.instant_current_a = instant_current_a;

    if ((g_adc_mon.last_current_filter_tick == 0U) ||
        ((now_ms - g_adc_mon.last_current_filter_tick) >= APP_CURRENT_FILTER_INTERVAL_MS))
    {
        g_adc_mon.last_current_filter_tick = now_ms;

        if (g_adc_mon.current_filter_count < APP_CURRENT_FILTER_WINDOW_SAMPLES)
        {
            g_adc_mon.current_filter_buffer[g_adc_mon.current_filter_index] = instant_current_a;
            g_adc_mon.current_filter_sum_a += instant_current_a;
            g_adc_mon.current_filter_count++;
        }
        else
        {
            g_adc_mon.current_filter_sum_a -= g_adc_mon.current_filter_buffer[g_adc_mon.current_filter_index];
            g_adc_mon.current_filter_buffer[g_adc_mon.current_filter_index] = instant_current_a;
            g_adc_mon.current_filter_sum_a += instant_current_a;
        }

        g_adc_mon.current_filter_index++;
        if (g_adc_mon.current_filter_index >= APP_CURRENT_FILTER_WINDOW_SAMPLES)
        {
            g_adc_mon.current_filter_index = 0U;
        }
    }

    if (g_adc_mon.current_filter_count > 0U)
    {
        g_adc_mon.data.current_a =
            g_adc_mon.current_filter_sum_a / (float)g_adc_mon.current_filter_count;
    }
    else
    {
        g_adc_mon.data.current_a = instant_current_a;
    }

    if (g_adc_mon.data.current_a < 0.0f)
    {
        g_adc_mon.data.current_abs_a = -g_adc_mon.data.current_a;
    }
    else
    {
        g_adc_mon.data.current_abs_a = g_adc_mon.data.current_a;
    }
    g_adc_mon.data.power_w = g_adc_mon.data.vbat_v * g_adc_mon.data.current_a;
}

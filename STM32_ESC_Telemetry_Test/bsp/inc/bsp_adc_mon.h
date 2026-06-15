#ifndef BSP_ADC_MON_H
#define BSP_ADC_MON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef struct
{
    uint16_t adc_i_raw;
    uint16_t adc_vbat_raw;
    float v_i_sense;
    float v_vbat_adc;
    float delta_i_v;
    float active_zero_offset_v;
    float instant_current_a;
    float current_a;
    float current_abs_a;
    float vbat_v;
    float power_w;
} BspAdcMonData_t;

void Bsp_AdcMon_Init(void);
void Bsp_AdcMon_Task(uint8_t motor_stopped, uint32_t now_ms);
const BspAdcMonData_t *Bsp_AdcMon_GetData(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_ADC_MON_H */

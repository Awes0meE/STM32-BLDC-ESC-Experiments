#ifndef BSP_DSHOT_H
#define BSP_DSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void Bsp_Dshot_Init(void);
void Bsp_Dshot_SetThrottle(uint16_t command);
uint16_t Bsp_Dshot_GetThrottle(void);
uint8_t Bsp_Dshot_IsBusy(void);
void Bsp_Dshot_Task(uint32_t now_ms);
void Bsp_Dshot_HandlePulseFinished(TIM_HandleTypeDef *htim);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DSHOT_H */

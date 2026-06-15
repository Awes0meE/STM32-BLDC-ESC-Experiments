#ifndef BSP_KEY_H
#define BSP_KEY_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void Bsp_Key_Init(void);
void Bsp_Key_Task(uint32_t now_ms);
uint8_t Bsp_Key_ConsumeShortPress(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_KEY_H */

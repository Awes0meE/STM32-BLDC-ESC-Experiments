#ifndef BSP_OLED_H
#define BSP_OLED_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void Bsp_Oled_Init(void);
void Bsp_Oled_Task(uint32_t now_ms);
uint8_t Bsp_Oled_IsReady(void);
void Bsp_Oled_ClearBuffer(void);
void Bsp_Oled_DrawLine(uint8_t line_index, const char *text);
void Bsp_Oled_Flush(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_OLED_H */

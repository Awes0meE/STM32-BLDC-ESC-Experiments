#ifndef BSP_HC05_H
#define BSP_HC05_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

void Bsp_Hc05_Init(void);
void Bsp_Hc05_HandleRxByte(uint8_t byte);
void Bsp_Hc05_HandleTxComplete(void);
void Bsp_Hc05_Write(const char *text);
uint8_t Bsp_Hc05_IsConnected(void);
uint8_t Bsp_Hc05_ConsumeStatusRequest(void);
uint8_t Bsp_Hc05_ConsumeStopRequest(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_HC05_H */

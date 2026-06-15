#ifndef APP_BUTTON_H
#define APP_BUTTON_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

typedef enum
{
    APP_BUTTON_EVENT_NONE = 0,
    APP_BUTTON_EVENT_SHORT_PRESS,
    APP_BUTTON_EVENT_LONG_PRESS
} AppButton_Event_t;

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
    GPIO_PinState active_level;
    uint32_t debounce_ms;
    uint32_t long_press_ms;
    uint32_t change_tick;
    uint32_t press_tick;
    uint8_t raw_pressed;
    uint8_t stable_pressed;
    uint8_t long_reported;
} AppButton_Handle_t;

void AppButton_Init(AppButton_Handle_t *button,
                    GPIO_TypeDef *port,
                    uint16_t pin,
                    GPIO_PinState active_level,
                    uint32_t debounce_ms,
                    uint32_t long_press_ms);

AppButton_Event_t AppButton_Update(AppButton_Handle_t *button, uint32_t now_ms);
uint8_t AppButton_IsPressed(const AppButton_Handle_t *button);

#ifdef __cplusplus
}
#endif

#endif /* APP_BUTTON_H */

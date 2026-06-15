#include "bsp_key.h"

#include <string.h>

#include "app_config.h"

typedef struct
{
    uint32_t change_tick;
    uint8_t raw_pressed;
    uint8_t stable_pressed;
    uint8_t short_press_pending;
} BspKeyContext_t;

static BspKeyContext_t g_key;

void Bsp_Key_Init(void)
{
    memset(&g_key, 0, sizeof(g_key));
}

void Bsp_Key_Task(uint32_t now_ms)
{
    uint8_t raw_pressed;

    raw_pressed = (HAL_GPIO_ReadPin(USER_KEY_GPIO_Port, USER_KEY_Pin) == GPIO_PIN_RESET) ? 1U : 0U;

    if (raw_pressed != g_key.raw_pressed)
    {
        g_key.raw_pressed = raw_pressed;
        g_key.change_tick = now_ms;
    }

    if ((now_ms - g_key.change_tick) < APP_KEY_DEBOUNCE_MS)
    {
        return;
    }

    if (raw_pressed != g_key.stable_pressed)
    {
        g_key.stable_pressed = raw_pressed;
        if (raw_pressed != 0U)
        {
            g_key.short_press_pending = 1U;
        }
    }
}

uint8_t Bsp_Key_ConsumeShortPress(void)
{
    uint8_t pressed = g_key.short_press_pending;
    g_key.short_press_pending = 0U;
    return pressed;
}

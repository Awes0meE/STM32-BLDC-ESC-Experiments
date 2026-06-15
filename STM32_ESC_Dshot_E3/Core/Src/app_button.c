#include "app_button.h"

#include <string.h>

void AppButton_Init(AppButton_Handle_t *button,
                    GPIO_TypeDef *port,
                    uint16_t pin,
                    GPIO_PinState active_level,
                    uint32_t debounce_ms,
                    uint32_t long_press_ms)
{
    if (button == NULL)
    {
        return;
    }

    memset(button, 0, sizeof(*button));
    button->port = port;
    button->pin = pin;
    button->active_level = active_level;
    button->debounce_ms = debounce_ms;
    button->long_press_ms = long_press_ms;
}

AppButton_Event_t AppButton_Update(AppButton_Handle_t *button, uint32_t now_ms)
{
    uint8_t raw_pressed;
    uint32_t pressed_ms;

    if ((button == NULL) || (button->port == NULL))
    {
        return APP_BUTTON_EVENT_NONE;
    }

    raw_pressed = (HAL_GPIO_ReadPin(button->port, button->pin) == button->active_level) ? 1U : 0U;

    if (raw_pressed != button->raw_pressed)
    {
        button->raw_pressed = raw_pressed;
        button->change_tick = now_ms;
    }

    if ((now_ms - button->change_tick) < button->debounce_ms)
    {
        return APP_BUTTON_EVENT_NONE;
    }

    if (raw_pressed != button->stable_pressed)
    {
        button->stable_pressed = raw_pressed;

        if (raw_pressed != 0U)
        {
            button->press_tick = now_ms;
            button->long_reported = 0U;
            return APP_BUTTON_EVENT_NONE;
        }

        pressed_ms = now_ms - button->press_tick;
        if ((button->long_reported == 0U) && (pressed_ms > 0U))
        {
            return APP_BUTTON_EVENT_SHORT_PRESS;
        }

        return APP_BUTTON_EVENT_NONE;
    }

    if ((button->stable_pressed != 0U) && (button->long_reported == 0U))
    {
        pressed_ms = now_ms - button->press_tick;
        if (pressed_ms >= button->long_press_ms)
        {
            button->long_reported = 1U;
            return APP_BUTTON_EVENT_LONG_PRESS;
        }
    }

    return APP_BUTTON_EVENT_NONE;
}

uint8_t AppButton_IsPressed(const AppButton_Handle_t *button)
{
    if (button == NULL)
    {
        return 0U;
    }

    return button->stable_pressed;
}

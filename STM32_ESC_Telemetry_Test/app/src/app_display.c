#include "app_display.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "bsp_oled.h"

typedef struct
{
    uint32_t last_update_ms;
} AppDisplayContext_t;

static AppDisplayContext_t g_display;

static void App_Display_FormatFixed2(char *dst, size_t len, float value);

void App_Display_Init(void)
{
    memset(&g_display, 0, sizeof(g_display));
    Bsp_Oled_Init();
}

void App_Display_Task(const AppDisplayModel_t *model, uint32_t now_ms)
{
    char line0[32];
    char line1[32];
    char line2[32];
    char line3[32];
    char line4[32];
    char vbat_text[12];
    char curr_text[12];

    Bsp_Oled_Task(now_ms);

    if ((model == NULL) || (Bsp_Oled_IsReady() == 0U))
    {
        return;
    }

    if ((g_display.last_update_ms != 0U) &&
        ((now_ms - g_display.last_update_ms) < APP_OLED_UPDATE_INTERVAL_MS))
    {
        return;
    }

    g_display.last_update_ms = now_ms;

    if (model->state == APP_STATE_ESTOP)
    {
        (void)snprintf(line0, sizeof(line0), "ESTOP");
    }
    else if (model->protect_active != 0U)
    {
        (void)snprintf(line0, sizeof(line0), "PROTECT %s",
                       App_Motor_GetProtectReasonName(model->protect_reason));
    }
    else if (model->state == APP_STATE_FAILSAFE)
    {
        (void)snprintf(line0, sizeof(line0), "FAILSAFE");
    }
    else if (model->rx_active != 0U)
    {
        (void)snprintf(line0, sizeof(line0), "RX OK");
    }
    else
    {
        (void)snprintf(line0, sizeof(line0), "RX LOST");
    }

    (void)snprintf(line1, sizeof(line1), "ARM %s", (model->armed != 0U) ? "ARM" : "DIS");
    (void)snprintf(line2, sizeof(line2), "THR %4uUS", (unsigned int)model->throttle_us);
    (void)snprintf(line3, sizeof(line3), "DSH %4u", (unsigned int)model->dshot_command);

    App_Display_FormatFixed2(vbat_text, sizeof(vbat_text), model->vbat_v);
    App_Display_FormatFixed2(curr_text, sizeof(curr_text), model->current_a);
    (void)snprintf(line4, sizeof(line4), "V %s I %s", vbat_text, curr_text);

    Bsp_Oled_ClearBuffer();
    Bsp_Oled_DrawLine(0U, line0);
    Bsp_Oled_DrawLine(1U, line1);
    Bsp_Oled_DrawLine(2U, line2);
    Bsp_Oled_DrawLine(3U, line3);
    Bsp_Oled_DrawLine(4U, line4);
    Bsp_Oled_Flush();
}

static void App_Display_FormatFixed2(char *dst, size_t len, float value)
{
    int32_t scaled;
    int32_t integer_part;
    int32_t fractional_part;

    if ((dst == NULL) || (len == 0U))
    {
        return;
    }

    scaled = (int32_t)(value * 100.0f + 0.5f);
    if (value < 0.0f)
    {
        scaled = (int32_t)(value * 100.0f - 0.5f);
    }
    integer_part = scaled / 100;
    fractional_part = scaled % 100;
    if (fractional_part < 0)
    {
        fractional_part = -fractional_part;
    }

    (void)snprintf(dst, len, "%ld.%02ld", (long)integer_part, (long)fractional_part);
}

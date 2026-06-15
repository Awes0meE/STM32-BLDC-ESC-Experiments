#include "bsp_oled.h"

#include <string.h>

#include "app_config.h"
#include "i2c.h"

typedef struct
{
    uint8_t buffer[128U * 8U];
    uint32_t last_init_attempt_tick;
    uint8_t ready;
} BspOledContext_t;

static BspOledContext_t g_oled;

static HAL_StatusTypeDef Bsp_Oled_WriteCommand(uint8_t command);
static HAL_StatusTypeDef Bsp_Oled_WriteData(const uint8_t *data, uint16_t size);
static void Bsp_Oled_PerformInit(void);
static void Bsp_Oled_DrawChar(uint8_t x, uint8_t y, char c);
static void Bsp_Oled_SetPixel(uint8_t x, uint8_t y, uint8_t on);
static void Bsp_Oled_GetGlyph5x7(char c, uint8_t glyph[5]);

void Bsp_Oled_Init(void)
{
    memset(&g_oled, 0, sizeof(g_oled));
}

void Bsp_Oled_Task(uint32_t now_ms)
{
    if (g_oled.ready != 0U)
    {
        return;
    }

    if (g_oled.last_init_attempt_tick == 0U)
    {
        if (now_ms < APP_OLED_POWERUP_DELAY_MS)
        {
            return;
        }
    }
    else if ((now_ms - g_oled.last_init_attempt_tick) < APP_OLED_RETRY_INTERVAL_MS)
    {
        return;
    }

    g_oled.last_init_attempt_tick = now_ms;
    Bsp_Oled_PerformInit();
}

uint8_t Bsp_Oled_IsReady(void)
{
    return g_oled.ready;
}

void Bsp_Oled_ClearBuffer(void)
{
    memset(g_oled.buffer, 0, sizeof(g_oled.buffer));
}

void Bsp_Oled_DrawLine(uint8_t line_index, const char *text)
{
    uint8_t x = 0U;
    uint8_t y;

    if ((g_oled.ready == 0U) || (text == NULL) || (line_index >= 5U))
    {
        return;
    }

    y = (uint8_t)(line_index * 11U);
    while ((*text != '\0') && (x <= 120U))
    {
        Bsp_Oled_DrawChar(x, y, *text);
        x = (uint8_t)(x + 7U);
        text++;
    }
}

void Bsp_Oled_Flush(void)
{
    uint8_t page;

    if (g_oled.ready == 0U)
    {
        return;
    }

    for (page = 0U; page < 8U; page++)
    {
        (void)Bsp_Oled_WriteCommand((uint8_t)(0xB0U | page));
        (void)Bsp_Oled_WriteCommand((uint8_t)(0x00U | (APP_OLED_COLUMN_OFFSET & 0x0FU)));
        (void)Bsp_Oled_WriteCommand((uint8_t)(0x10U | ((APP_OLED_COLUMN_OFFSET >> 4U) & 0x0FU)));
        (void)Bsp_Oled_WriteData(&g_oled.buffer[page * 128U], 128U);
    }
}

static HAL_StatusTypeDef Bsp_Oled_WriteCommand(uint8_t command)
{
    uint8_t frame[2];

    if (g_oled.ready == 0U)
    {
        return HAL_ERROR;
    }

    frame[0] = 0x00U;
    frame[1] = command;
    return HAL_I2C_Master_Transmit(&hi2c1, APP_OLED_I2C_ADDR, frame, 2U, 50U);
}

static HAL_StatusTypeDef Bsp_Oled_WriteData(const uint8_t *data, uint16_t size)
{
    uint8_t frame[129];

    if ((g_oled.ready == 0U) || (data == NULL) || (size > 128U))
    {
        return HAL_ERROR;
    }

    frame[0] = 0x40U;
    memcpy(&frame[1], data, size);
    return HAL_I2C_Master_Transmit(&hi2c1, APP_OLED_I2C_ADDR, frame, (uint16_t)(size + 1U), 50U);
}

static void Bsp_Oled_PerformInit(void)
{
    static const uint8_t init_cmds[] = {
        0xAEU, 0x20U, 0x02U, 0xB0U, 0xC8U, 0x00U, 0x10U, 0x40U,
        0x81U, 0x7FU, 0xA1U, 0xA6U, 0xA8U, 0x3FU, 0xA4U, 0xD3U,
        0x00U, 0xD5U, 0x80U, 0xD9U, 0xF1U, 0xDAU, 0x12U, 0xDBU,
        0x20U, 0x8DU, 0x14U, 0xAFU
    };
    uint8_t i;

    g_oled.ready = 0U;

    if (HAL_I2C_IsDeviceReady(&hi2c1, APP_OLED_I2C_ADDR, 2U, 100U) != HAL_OK)
    {
        return;
    }

    g_oled.ready = 1U;

    for (i = 0U; i < (uint8_t)(sizeof(init_cmds) / sizeof(init_cmds[0])); i++)
    {
        if (Bsp_Oled_WriteCommand(init_cmds[i]) != HAL_OK)
        {
            g_oled.ready = 0U;
            return;
        }
    }

    Bsp_Oled_ClearBuffer();
    Bsp_Oled_Flush();
}

static void Bsp_Oled_DrawChar(uint8_t x, uint8_t y, char c)
{
    uint8_t glyph[5];
    uint8_t col;
    uint8_t row;

    Bsp_Oled_GetGlyph5x7(c, glyph);

    for (col = 0U; col < 5U; col++)
    {
        for (row = 0U; row < 7U; row++)
        {
            if ((glyph[col] & (uint8_t)(1U << row)) != 0U)
            {
                Bsp_Oled_SetPixel((uint8_t)(x + col), (uint8_t)(y + row), 1U);
                Bsp_Oled_SetPixel((uint8_t)(x + col + 1U), (uint8_t)(y + row), 1U);
            }
        }
    }
}

static void Bsp_Oled_SetPixel(uint8_t x, uint8_t y, uint8_t on)
{
    uint16_t index;
    uint8_t mask;

    if ((x >= 128U) || (y >= 64U))
    {
        return;
    }

    index = (uint16_t)((y / 8U) * 128U + x);
    mask = (uint8_t)(1U << (y % 8U));

    if (on != 0U)
    {
        g_oled.buffer[index] |= mask;
    }
    else
    {
        g_oled.buffer[index] &= (uint8_t)(~mask);
    }
}

static void Bsp_Oled_GetGlyph5x7(char c, uint8_t glyph[5])
{
    if (glyph == NULL)
    {
        return;
    }

    memset(glyph, 0, 5U);

    switch (c)
    {
    case '-': glyph[0] = 0x08U; glyph[1] = 0x08U; glyph[2] = 0x08U; glyph[3] = 0x08U; glyph[4] = 0x08U; break;
    case '.': glyph[1] = 0x60U; glyph[2] = 0x60U; break;
    case '/': glyph[0] = 0x40U; glyph[1] = 0x20U; glyph[2] = 0x10U; glyph[3] = 0x08U; glyph[4] = 0x04U; break;
    case '0': glyph[0] = 0x3EU; glyph[1] = 0x51U; glyph[2] = 0x49U; glyph[3] = 0x45U; glyph[4] = 0x3EU; break;
    case '1': glyph[0] = 0x00U; glyph[1] = 0x42U; glyph[2] = 0x7FU; glyph[3] = 0x40U; glyph[4] = 0x00U; break;
    case '2': glyph[0] = 0x42U; glyph[1] = 0x61U; glyph[2] = 0x51U; glyph[3] = 0x49U; glyph[4] = 0x46U; break;
    case '3': glyph[0] = 0x21U; glyph[1] = 0x41U; glyph[2] = 0x45U; glyph[3] = 0x4BU; glyph[4] = 0x31U; break;
    case '4': glyph[0] = 0x18U; glyph[1] = 0x14U; glyph[2] = 0x12U; glyph[3] = 0x7FU; glyph[4] = 0x10U; break;
    case '5': glyph[0] = 0x27U; glyph[1] = 0x45U; glyph[2] = 0x45U; glyph[3] = 0x45U; glyph[4] = 0x39U; break;
    case '6': glyph[0] = 0x3CU; glyph[1] = 0x4AU; glyph[2] = 0x49U; glyph[3] = 0x49U; glyph[4] = 0x30U; break;
    case '7': glyph[0] = 0x01U; glyph[1] = 0x71U; glyph[2] = 0x09U; glyph[3] = 0x05U; glyph[4] = 0x03U; break;
    case '8': glyph[0] = 0x36U; glyph[1] = 0x49U; glyph[2] = 0x49U; glyph[3] = 0x49U; glyph[4] = 0x36U; break;
    case '9': glyph[0] = 0x06U; glyph[1] = 0x49U; glyph[2] = 0x49U; glyph[3] = 0x29U; glyph[4] = 0x1EU; break;
    case 'A': glyph[0] = 0x7EU; glyph[1] = 0x11U; glyph[2] = 0x11U; glyph[3] = 0x11U; glyph[4] = 0x7EU; break;
    case 'D': glyph[0] = 0x7FU; glyph[1] = 0x41U; glyph[2] = 0x41U; glyph[3] = 0x22U; glyph[4] = 0x1CU; break;
    case 'E': glyph[0] = 0x7FU; glyph[1] = 0x49U; glyph[2] = 0x49U; glyph[3] = 0x49U; glyph[4] = 0x41U; break;
    case 'F': glyph[0] = 0x7FU; glyph[1] = 0x09U; glyph[2] = 0x09U; glyph[3] = 0x09U; glyph[4] = 0x01U; break;
    case 'H': glyph[0] = 0x7FU; glyph[1] = 0x08U; glyph[2] = 0x08U; glyph[3] = 0x08U; glyph[4] = 0x7FU; break;
    case 'I': glyph[0] = 0x00U; glyph[1] = 0x41U; glyph[2] = 0x7FU; glyph[3] = 0x41U; glyph[4] = 0x00U; break;
    case 'K': glyph[0] = 0x7FU; glyph[1] = 0x08U; glyph[2] = 0x14U; glyph[3] = 0x22U; glyph[4] = 0x41U; break;
    case 'L': glyph[0] = 0x7FU; glyph[1] = 0x40U; glyph[2] = 0x40U; glyph[3] = 0x40U; glyph[4] = 0x40U; break;
    case 'M': glyph[0] = 0x7FU; glyph[1] = 0x02U; glyph[2] = 0x0CU; glyph[3] = 0x02U; glyph[4] = 0x7FU; break;
    case 'O': glyph[0] = 0x3EU; glyph[1] = 0x41U; glyph[2] = 0x41U; glyph[3] = 0x41U; glyph[4] = 0x3EU; break;
    case 'P': glyph[0] = 0x7FU; glyph[1] = 0x09U; glyph[2] = 0x09U; glyph[3] = 0x09U; glyph[4] = 0x06U; break;
    case 'R': glyph[0] = 0x7FU; glyph[1] = 0x09U; glyph[2] = 0x19U; glyph[3] = 0x29U; glyph[4] = 0x46U; break;
    case 'S': glyph[0] = 0x46U; glyph[1] = 0x49U; glyph[2] = 0x49U; glyph[3] = 0x49U; glyph[4] = 0x31U; break;
    case 'T': glyph[0] = 0x01U; glyph[1] = 0x01U; glyph[2] = 0x7FU; glyph[3] = 0x01U; glyph[4] = 0x01U; break;
    case 'U': glyph[0] = 0x3FU; glyph[1] = 0x40U; glyph[2] = 0x40U; glyph[3] = 0x40U; glyph[4] = 0x3FU; break;
    case 'V': glyph[0] = 0x1FU; glyph[1] = 0x20U; glyph[2] = 0x40U; glyph[3] = 0x20U; glyph[4] = 0x1FU; break;
    case 'W': glyph[0] = 0x7FU; glyph[1] = 0x20U; glyph[2] = 0x18U; glyph[3] = 0x20U; glyph[4] = 0x7FU; break;
    case 'X': glyph[0] = 0x63U; glyph[1] = 0x14U; glyph[2] = 0x08U; glyph[3] = 0x14U; glyph[4] = 0x63U; break;
    case 'Y': glyph[0] = 0x03U; glyph[1] = 0x04U; glyph[2] = 0x78U; glyph[3] = 0x04U; glyph[4] = 0x03U; break;
    default: break;
    }
}

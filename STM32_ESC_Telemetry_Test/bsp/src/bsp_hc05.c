#include "bsp_hc05.h"

#include <string.h>

#include "app_config.h"
#include "usart.h"

typedef struct
{
    char command_buffer[24];
    uint8_t tx_ring[1024];
    uint8_t tx_chunk[64];
    uint16_t tx_head;
    uint16_t tx_tail;
    uint16_t tx_chunk_len;
    uint8_t command_index;
    uint8_t status_request_pending;
    uint8_t stop_request_pending;
    uint8_t tx_busy;
} BspHc05Context_t;

static BspHc05Context_t g_hc05;

static void Bsp_Hc05_ProcessCommand(const char *command_text);
static void Bsp_Hc05_StartNextTx(void);

void Bsp_Hc05_Init(void)
{
    memset(&g_hc05, 0, sizeof(g_hc05));
}

void Bsp_Hc05_HandleRxByte(uint8_t byte)
{
    char c = (char)byte;

    if ((c == '\r') || (c == '\n'))
    {
        if (g_hc05.command_index > 0U)
        {
            g_hc05.command_buffer[g_hc05.command_index] = '\0';
            Bsp_Hc05_ProcessCommand(g_hc05.command_buffer);
            g_hc05.command_index = 0U;
        }
        return;
    }

    if ((c >= 'a') && (c <= 'z'))
    {
        c = (char)(c - 'a' + 'A');
    }

    if (((c >= 'A') && (c <= 'Z')) || ((c >= '0') && (c <= '9')) || (c == '_'))
    {
        if (g_hc05.command_index < (uint8_t)(sizeof(g_hc05.command_buffer) - 1U))
        {
            g_hc05.command_buffer[g_hc05.command_index++] = c;
            g_hc05.command_buffer[g_hc05.command_index] = '\0';
        }
        else
        {
            g_hc05.command_index = 0U;
        }
    }
    else
    {
        g_hc05.command_index = 0U;
    }
}

void Bsp_Hc05_HandleTxComplete(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    g_hc05.tx_head = (uint16_t)((g_hc05.tx_head + g_hc05.tx_chunk_len) % (uint16_t)sizeof(g_hc05.tx_ring));
    g_hc05.tx_chunk_len = 0U;
    g_hc05.tx_busy = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    Bsp_Hc05_StartNextTx();
}

void Bsp_Hc05_Write(const char *text)
{
    size_t length;
    size_t i;
    uint32_t primask;

    if (text == NULL)
    {
        return;
    }

    length = strlen(text);
    if (length == 0U)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    for (i = 0U; i < length; i++)
    {
        uint16_t next_tail = (uint16_t)((g_hc05.tx_tail + 1U) % (uint16_t)sizeof(g_hc05.tx_ring));
        if (next_tail == g_hc05.tx_head)
        {
            break;
        }

        g_hc05.tx_ring[g_hc05.tx_tail] = (uint8_t)text[i];
        g_hc05.tx_tail = next_tail;
    }
    if (primask == 0U)
    {
        __enable_irq();
    }

    Bsp_Hc05_StartNextTx();
}

uint8_t Bsp_Hc05_IsConnected(void)
{
    return (HAL_GPIO_ReadPin(HC05_STATE_GPIO_Port, HC05_STATE_Pin) == GPIO_PIN_SET) ?
        APP_HC05_CONNECTED_LEVEL : 0U;
}

uint8_t Bsp_Hc05_ConsumeStatusRequest(void)
{
    uint8_t pending = g_hc05.status_request_pending;
    g_hc05.status_request_pending = 0U;
    return pending;
}

uint8_t Bsp_Hc05_ConsumeStopRequest(void)
{
    uint8_t pending = g_hc05.stop_request_pending;
    g_hc05.stop_request_pending = 0U;
    return pending;
}

static void Bsp_Hc05_ProcessCommand(const char *command_text)
{
    if (command_text == NULL)
    {
        return;
    }

    if (strcmp(command_text, "STATUS") == 0)
    {
        g_hc05.status_request_pending = 1U;
    }
    else if (strcmp(command_text, "STOP") == 0)
    {
        g_hc05.stop_request_pending = 1U;
    }
}

static void Bsp_Hc05_StartNextTx(void)
{
    uint16_t available;
    uint16_t i;
    uint16_t head;
    uint32_t primask;

    if (g_hc05.tx_busy != 0U)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    if (g_hc05.tx_head == g_hc05.tx_tail)
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return;
    }

    if (g_hc05.tx_tail > g_hc05.tx_head)
    {
        available = (uint16_t)(g_hc05.tx_tail - g_hc05.tx_head);
    }
    else
    {
        available = (uint16_t)(sizeof(g_hc05.tx_ring) - g_hc05.tx_head);
    }

    if (available > (uint16_t)sizeof(g_hc05.tx_chunk))
    {
        available = (uint16_t)sizeof(g_hc05.tx_chunk);
    }

    head = g_hc05.tx_head;
    for (i = 0U; i < available; i++)
    {
        g_hc05.tx_chunk[i] = g_hc05.tx_ring[head + i];
    }
    g_hc05.tx_chunk_len = available;
    g_hc05.tx_busy = 1U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    if (HAL_UART_Transmit_IT(&huart1, g_hc05.tx_chunk, available) != HAL_OK)
    {
        primask = __get_PRIMASK();
        __disable_irq();
        g_hc05.tx_busy = 0U;
        g_hc05.tx_chunk_len = 0U;
        if (primask == 0U)
        {
            __enable_irq();
        }
    }
}

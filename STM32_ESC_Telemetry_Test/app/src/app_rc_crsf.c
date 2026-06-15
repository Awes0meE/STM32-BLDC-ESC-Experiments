#include "app_rc_crsf.h"

#include <string.h>

typedef struct
{
    uint8_t frame_buffer[64];
    uint8_t frame_index;
    uint8_t expected_size;
    AppRcCrsfData_t data;
} AppRcCrsfContext_t;

static AppRcCrsfContext_t g_crsf;

static uint8_t App_RcCrsf_Crc8DvbS2(uint8_t crc, uint8_t data);
static uint8_t App_RcCrsf_ComputeFrameCrc(const uint8_t *frame, uint8_t frame_size);
static void App_RcCrsf_ProcessFrame(const uint8_t *frame, uint8_t frame_size);
static void App_RcCrsf_DecodeChannels(const uint8_t *payload, uint8_t payload_size);
static uint16_t App_RcCrsf_RawToUs(uint16_t raw_value);
static uint8_t App_RcCrsf_IsValidAddress(uint8_t address);

void App_RcCrsf_Init(void)
{
    memset(&g_crsf, 0, sizeof(g_crsf));
}

void App_RcCrsf_HandleRxByte(uint8_t byte)
{
    g_crsf.data.diag.rx_byte_count++;

    if (g_crsf.frame_index == 0U)
    {
        if (App_RcCrsf_IsValidAddress(byte) == 0U)
        {
            return;
        }

        g_crsf.frame_buffer[g_crsf.frame_index++] = byte;
        return;
    }

    if (g_crsf.frame_index == 1U)
    {
        if ((byte < 2U) || (byte > (uint8_t)(sizeof(g_crsf.frame_buffer) - 2U)))
        {
            g_crsf.data.diag.size_error_count++;
            g_crsf.frame_index = 0U;
            g_crsf.expected_size = 0U;

            if (App_RcCrsf_IsValidAddress(byte) != 0U)
            {
                g_crsf.frame_buffer[g_crsf.frame_index++] = byte;
            }
            return;
        }

        g_crsf.frame_buffer[g_crsf.frame_index++] = byte;
        g_crsf.expected_size = (uint8_t)(byte + 2U);
        return;
    }

    g_crsf.frame_buffer[g_crsf.frame_index++] = byte;

    if (g_crsf.frame_index >= sizeof(g_crsf.frame_buffer))
    {
        g_crsf.data.diag.size_error_count++;
        g_crsf.frame_index = 0U;
        g_crsf.expected_size = 0U;
        return;
    }

    if ((g_crsf.expected_size != 0U) && (g_crsf.frame_index == g_crsf.expected_size))
    {
        App_RcCrsf_ProcessFrame(g_crsf.frame_buffer, g_crsf.expected_size);
        g_crsf.frame_index = 0U;
        g_crsf.expected_size = 0U;
    }
}

void App_RcCrsf_ReportUartError(uint32_t status_reg)
{
    g_crsf.data.diag.uart_error_count++;
    g_crsf.data.diag.last_error_sr = status_reg;
}

void App_RcCrsf_GetSnapshot(AppRcCrsfData_t *out)
{
    uint32_t primask;

    if (out == NULL)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    memcpy(out, &g_crsf.data, sizeof(*out));
    if (primask == 0U)
    {
        __enable_irq();
    }
}

uint8_t App_RcCrsf_HasFreshFrame(uint32_t now_ms, uint32_t timeout_ms)
{
    int32_t dt_ms;

    if (g_crsf.data.frame_ok == 0U)
    {
        return 0U;
    }

    dt_ms = (int32_t)(now_ms - g_crsf.data.last_frame_ms);
    return (dt_ms <= (int32_t)timeout_ms) ? 1U : 0U;
}

static uint8_t App_RcCrsf_Crc8DvbS2(uint8_t crc, uint8_t data)
{
    uint8_t i;

    crc ^= data;
    for (i = 0U; i < 8U; i++)
    {
        if ((crc & 0x80U) != 0U)
        {
            crc = (uint8_t)((crc << 1U) ^ 0xD5U);
        }
        else
        {
            crc <<= 1U;
        }
    }

    return crc;
}

static uint8_t App_RcCrsf_ComputeFrameCrc(const uint8_t *frame, uint8_t frame_size)
{
    uint8_t crc = 0U;
    uint8_t i;

    for (i = 2U; i < (uint8_t)(frame_size - 1U); i++)
    {
        crc = App_RcCrsf_Crc8DvbS2(crc, frame[i]);
    }

    return crc;
}

static void App_RcCrsf_ProcessFrame(const uint8_t *frame, uint8_t frame_size)
{
    const uint8_t frame_type = frame[2];
    const uint8_t payload_size = (uint8_t)(frame[1] - 2U);
    const uint8_t *payload = &frame[3];

    if ((frame == NULL) || (frame_size < 5U))
    {
        return;
    }

    if (App_RcCrsf_ComputeFrameCrc(frame, frame_size) != frame[frame_size - 1U])
    {
        g_crsf.data.diag.crc_error_count++;
        return;
    }

    switch (frame_type)
    {
    case 0x16U:
        App_RcCrsf_DecodeChannels(payload, payload_size);
        g_crsf.data.frame_ok = 1U;
        g_crsf.data.last_frame_ms = HAL_GetTick();
        g_crsf.data.diag.valid_frame_count++;
        break;

    case 0x14U:
        if (payload_size >= 10U)
        {
            g_crsf.data.link_stats.uplink_rssi_1 = payload[0];
            g_crsf.data.link_stats.uplink_rssi_2 = payload[1];
            g_crsf.data.link_stats.uplink_link_quality = payload[2];
            g_crsf.data.link_stats.uplink_snr = (int8_t)payload[3];
            g_crsf.data.link_stats.active_antenna = payload[4];
            g_crsf.data.link_stats.rf_mode = payload[5];
            g_crsf.data.link_stats.uplink_tx_power = payload[6];
            g_crsf.data.link_stats.downlink_rssi = payload[7];
            g_crsf.data.link_stats.downlink_link_quality = payload[8];
            g_crsf.data.link_stats.downlink_snr = (int8_t)payload[9];
            g_crsf.data.link_stats.valid = 1U;
        }
        break;

    default:
        break;
    }
}

static void App_RcCrsf_DecodeChannels(const uint8_t *payload, uint8_t payload_size)
{
    uint32_t bit_index = 0U;
    uint8_t ch;
    uint8_t bit;

    if ((payload == NULL) || (payload_size < 22U))
    {
        return;
    }

    for (ch = 0U; ch < APP_RC_CHANNEL_COUNT; ch++)
    {
        uint16_t raw_value = 0U;

        for (bit = 0U; bit < 11U; bit++)
        {
            const uint32_t absolute_bit = bit_index + bit;
            const uint8_t payload_index = (uint8_t)(absolute_bit / 8U);
            const uint8_t payload_mask = (uint8_t)(1U << (absolute_bit % 8U));

            if ((payload[payload_index] & payload_mask) != 0U)
            {
                raw_value |= (uint16_t)(1U << bit);
            }
        }

        g_crsf.data.raw[ch] = raw_value;
        g_crsf.data.us[ch] = App_RcCrsf_RawToUs(raw_value);
        bit_index += 11U;
    }
}

static uint16_t App_RcCrsf_RawToUs(uint16_t raw_value)
{
    const uint16_t raw_min = 172U;
    const uint16_t raw_max = 1811U;
    const uint16_t us_min = APP_RC_MIN_ENDPOINT_US;
    const uint16_t us_max = APP_RC_MAX_ENDPOINT_US;
    uint32_t scaled;

    if (raw_value <= raw_min)
    {
        return us_min;
    }

    if (raw_value >= raw_max)
    {
        return us_max;
    }

    scaled = (uint32_t)(raw_value - raw_min) * (uint32_t)(us_max - us_min);
    scaled /= (uint32_t)(raw_max - raw_min);
    return (uint16_t)(us_min + scaled);
}

static uint8_t App_RcCrsf_IsValidAddress(uint8_t address)
{
    switch (address)
    {
    case 0x00U:
    case 0xC8U:
    case 0xEAU:
    case 0xECU:
    case 0xEEU:
        return 1U;

    default:
        return 0U;
    }
}

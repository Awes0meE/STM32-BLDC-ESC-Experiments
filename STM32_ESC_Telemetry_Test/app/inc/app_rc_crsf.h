#ifndef APP_RC_CRSF_H
#define APP_RC_CRSF_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "app_config.h"

typedef struct
{
    uint8_t uplink_rssi_1;
    uint8_t uplink_rssi_2;
    uint8_t uplink_link_quality;
    int8_t uplink_snr;
    uint8_t active_antenna;
    uint8_t rf_mode;
    uint8_t uplink_tx_power;
    uint8_t downlink_rssi;
    uint8_t downlink_link_quality;
    int8_t downlink_snr;
    uint8_t valid;
} AppCrsfLinkStats_t;

typedef struct
{
    uint32_t rx_byte_count;
    uint32_t valid_frame_count;
    uint32_t crc_error_count;
    uint32_t size_error_count;
    uint32_t uart_error_count;
    uint32_t last_error_sr;
} AppCrsfDiag_t;

typedef struct
{
    uint16_t raw[APP_RC_CHANNEL_COUNT];
    uint16_t us[APP_RC_CHANNEL_COUNT];
    uint32_t last_frame_ms;
    uint8_t frame_ok;
    AppCrsfLinkStats_t link_stats;
    AppCrsfDiag_t diag;
} AppRcCrsfData_t;

void App_RcCrsf_Init(void);
void App_RcCrsf_HandleRxByte(uint8_t byte);
void App_RcCrsf_ReportUartError(uint32_t status_reg);
void App_RcCrsf_GetSnapshot(AppRcCrsfData_t *out);
uint8_t App_RcCrsf_HasFreshFrame(uint32_t now_ms, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif /* APP_RC_CRSF_H */

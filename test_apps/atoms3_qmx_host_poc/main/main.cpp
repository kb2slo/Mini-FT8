// Bench POC only — not shipped code, not wired into the main build.
//
// Question this answers: can an AtomS3 Lite's USB-C act as a USB HOST,
// enumerate a QMX/QDX, and successfully deliver a real CAT command to it?
//
// Deliberately observable WITHOUT a serial monitor: this operator's bench
// has no way to watch UART/USB-Serial-JTOAG logs while the AtomS3's only
// USB-C port is busy being a host connected to the QMX (same port,
// mutually exclusive roles). So instead of logging pass/fail, this sends a
// real Kenwood-style CAT command that changes the QMX's OWN on-screen VFO
// frequency — the QMX's own display is the test result, no computer needed.
//
// Sets VFO A to 7,074,000 Hz (a recognizable, easy-to-eyeball round number
// on the 40m FT8 calling frequency) via the exact same command sequence
// main/radio_control_qmx.cpp already uses and has proven on the ADV:
// MD6; FR0; FT0; FA00007074000; — mode, RX VFO=A, TX VFO=A, then frequency.
// Entirely receive-side: no TX;, nothing keys the transmitter. Safe with
// no antenna/dummy load connected.
//
// PASS: the QMX's own screen shows 7.074.000 (or however it renders that
// frequency) within a couple seconds of power-up with USB-C connected.
// FAIL: cdc_acm_host_open() never succeeds (logged, but if you can't see
// logs either, "QMX's display never changes" is itself the fail signal).

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/cdc_acm_host.h"

static const char* TAG = "POC";

static constexpr uint16_t kQmxVid = 0x0483;
static constexpr uint16_t kQmxPid = 0xA34C;

static void usb_lib_task(void*) {
    usb_host_config_t host_config = {};
    host_config.skip_phy_setup = false;
    host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
    // Same FIFO split as main/stream_uac.cpp — chip-level ESP32-S3 setting,
    // proven with QMX on the ADV, not board-specific.
    host_config.fifo_settings_custom.rx_fifo_lines   = 91;
    host_config.fifo_settings_custom.nptx_fifo_lines = 18;
    host_config.fifo_settings_custom.ptx_fifo_lines  = 91;

    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_host_install FAILED: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "usb_host_install OK");

    while (1) {
        uint32_t event_flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(200), &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

static bool qmx_send(cdc_acm_dev_hdl_t hdl, const char* cmd) {
    esp_err_t err = cdc_acm_host_data_tx_blocking(
        hdl, reinterpret_cast<const uint8_t*>(cmd), strlen(cmd), 200);
    ESP_LOGI(TAG, "sent '%s' -> %s", cmd, esp_err_to_name(err));
    return err == ESP_OK;
}

extern "C" void app_main(void) {
    ESP_LOGI(TAG, "=== AtomS3 Lite QMX CAT POC ===");

    xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, NULL, 5, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(500));  // let the host library task install first

    esp_err_t err = cdc_acm_host_install(NULL);  // defaults are fine for one shot
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cdc_acm_host_install FAILED: %s", esp_err_to_name(err));
        return;
    }

    cdc_acm_host_device_config_t dev_cfg = {
        .connection_timeout_ms = 5000,
        .out_buffer_size = 64,
        .in_buffer_size = 64,
        .event_cb = NULL,  // write-only test, no need to observe device events
        .data_cb = NULL,   // write-only test, never reading a response
        .user_arg = NULL,
    };

    ESP_LOGI(TAG, "waiting up to 5s for QMX (vid=0x%04x pid=0x%04x) -- plug it in now",
             kQmxVid, kQmxPid);
    cdc_acm_dev_hdl_t hdl = NULL;
    err = cdc_acm_host_open(kQmxVid, kQmxPid, 0, &dev_cfg, &hdl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "cdc_acm_host_open FAILED: %s -- QMX never enumerated", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "QMX CDC interface open -- sending CAT sequence");

    // Same sequence and exact FA string format as radio_control_qmx.cpp's
    // qmx_sync_frequency_mode(): MD6; FR0; FT0; FA<11-digit-Hz>;
    qmx_send(hdl, "MD6;");
    vTaskDelay(pdMS_TO_TICKS(100));
    qmx_send(hdl, "FR0;");
    vTaskDelay(pdMS_TO_TICKS(100));
    qmx_send(hdl, "FT0;");
    vTaskDelay(pdMS_TO_TICKS(100));

    char fa[32];
    snprintf(fa, sizeof(fa), "FA%011d;", 7074000);
    qmx_send(hdl, fa);

    ESP_LOGI(TAG, "Done. Check the QMX's own screen for 7.074.000 -- that's the test result.");
}

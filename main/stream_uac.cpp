#include "stream_uac.h"
#include "ft8_audio_pipeline.h"
#include "resample.h"
#include "dds_q15.h"
#include "feature_flags.h"
#include "protocol.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "usb/usb_host.h"
#include "usb/uac_host.h"
#include "usb/cdc_acm_host.h"
#include "usb/usb_types_ch9.h"

#include <cstring>
#include <cmath>
#include <inttypes.h>

static const char* TAG = "UAC_STREAM";
extern void log_heap(const char* tag);

// External references from main.cpp
extern bool g_streaming;
extern volatile bool g_cdc_initial_sync_pending;
int64_t rtc_now_ms();

// Task priorities and stack sizes
#define USB_HOST_TASK_PRIORITY  5
#define UAC_TASK_PRIORITY       5
// Below the USB host task (5) so CDC-ACM CAT TX to the QMX is never
// preempted by audio-pipeline work.
#define UAC_STREAM_TASK_PRIORITY 4
#define TASK_STACK_SIZE         4096
#define STREAM_TASK_STACK_SIZE  8192

// UAC read buffer size (bytes) - must be multiple of 288 (USB transfer size at 48kHz/24bit/stereo).
// 288 bytes = 48 stereo samples per 1 ms USB transfer; 2304 = 8 transfers = 8 ms of audio.
// Kept at 8 transfers: the DMA buffer lives in BSS (DMA_ATTR), so larger
// values directly increase static internal DRAM use.
#define UAC_READ_BUFFER_SIZE    2304

// USB host DMA buffer — placed in DMA-capable BSS via DMA_ATTR so task
// startup doesn't depend on finding a large DMA-aligned heap block under
// fragmentation (heap_caps_malloc(MALLOC_CAP_DMA) can fail even when raw
// free bytes look sufficient because alignment requirements eat contiguous runs).
static DMA_ATTR uint8_t s_usb_buffer[UAC_READ_BUFFER_SIZE];

typedef struct {
    uint32_t sample_freq;
    uint8_t bit_resolution;
    uint8_t channels;
} uac_active_format_t;

// Event types for internal queue
typedef enum {
    UAC_EVT_DRIVER,
    UAC_EVT_DEVICE,
    UAC_EVT_STOP,
} uac_event_type_t;

typedef struct {
    uac_event_type_t type;
    union {
        struct {
            uint8_t addr;
            uint8_t iface_num;
            uac_host_driver_event_t event;
        } driver;
        struct {
            uac_host_device_handle_t handle;
            uac_host_device_event_t event;
        } device;
    };
} uac_event_t;

// Global state
static uac_stream_state_t s_state = UAC_STATE_IDLE;
static QueueHandle_t s_event_queue = NULL;
static uac_host_device_handle_t s_mic_handle = NULL;
static cdc_acm_dev_hdl_t s_cdc_handle = NULL;
static TaskHandle_t s_usb_task_handle = NULL;
static TaskHandle_t s_uac_task_handle = NULL;
static volatile bool s_host_installed = false;

// Speaker (UAC OUT) is captured and fully allocated during enumeration.
// QDX transmission only resumes or suspends the pre-opened endpoint.
static uint8_t s_spk_addr = 0;
static uint8_t s_spk_iface = 0;
static bool s_spk_known = false;
static uac_host_device_handle_t s_spk_handle = NULL;
static TaskHandle_t s_spk_writer_task = NULL;
static volatile bool s_spk_writer_stop = false;  // permanent shutdown
static volatile bool s_spk_writer_run = false;   // per-TX gate
static uint8_t* s_spk_pump_buf = nullptr;
static uint32_t s_spk_packets_sent = 0;
static uint32_t s_spk_write_errors = 0;
static TaskHandle_t s_stream_task_handle = NULL;
static volatile bool s_stop_requested = false;
static char s_status_string[64] = "Idle";
static uac_stream_profile_t s_profile = UAC_PROFILE_QMX;
static uac_active_format_t s_format = {
    .sample_freq = UAC_SAMPLE_RATE,
    .bit_resolution = UAC_BIT_RESOLUTION,
    .channels = UAC_CHANNELS,
};
static bool s_cdc_installed = false;
static int s_cdc_iface = -1;
static int s_cdc_iface_hint = -1;
static int64_t s_cdc_last_attempt_ms = 0;
static constexpr uint16_t k_qmx_vid = 0x0483;
static constexpr uint16_t k_qmx_pid = 0xA34C;

// Debug display buffers
static char s_debug_line1[64] = "";
static char s_debug_line2[64] = "";

// Resampler state
static resample_state_t s_resample_state;

// Forward declarations
static void usb_lib_task(void* arg);
static void uac_lib_task(void* arg);
static void stream_uac_task(void* arg);
static void cdc_close(void);
static void cdc_try_open(void);
static void cdc_event_cb(const cdc_acm_host_dev_event_data_t* event, void* user_ctx);

// Speaker constants and event callback forward declaration. The
// constants are referenced from uac_lib_task's TX_CONNECTED handler
// (which now pre-opens the speaker on enum) as well as from the
// pump task itself further down in the file.
// Speaker ringbuffer must hold one full pump write atomically (push
// is all-or-none in xRingbuffer). All speaker resources are pre-
// allocated at TX_CONNECTED enum time (handle, ringbuffer, xfer URBs,
// pump buffer, writer task) so per-TX is allocation-free.
//   ringbuffer: 4 KB  (~14 ms of audio at 288 B/ms)
//   pump:       1.7 KB (6 packets × 288 B) — must fit in ringbuffer
//   stack:      3 KB  for the writer task
static const uint32_t SPK_BUFFER_SIZE      = 4000;      // driver ringbuffer
static const uint32_t SPK_BUFFER_THRESHOLD = 600;       // ~2 ms at 48k/24/stereo
static const uint32_t SPK_WRITE_TIMEOUT_MS = 200;
static const uint32_t SPK_PACKET_BYTES     = 288;       // 48 stereo frames × 6 B
static const uint32_t SPK_PUMP_PACKETS     = 6;         // 6 packets per write = 1728 B
static const uint32_t SPK_PUMP_BYTES       = SPK_PACKET_BYTES * SPK_PUMP_PACKETS;  // 1728
static const uint32_t SPK_FRAME_BYTES      = 6;         // L+R, 24-bit each
static void spk_event_cb(uac_host_device_handle_t dev,
                         const uac_host_device_event_t event,
                         void* arg);
static void spk_writer_task(void* arg);
static void cdc_new_dev_cb(usb_device_handle_t usb_dev);
static int uac_read_ft8_samples(void* ctx, float* out, int max_samples);
static bool uac_should_stop(void* ctx);
static void uac_on_block_processed(void* ctx);

static const char* profile_name(uac_stream_profile_t profile) {
    switch (profile) {
    case UAC_PROFILE_QMX:
        return "qmx_uac";
    case UAC_PROFILE_GENERIC_USB:
        return "usb_uac_generic";
    default:
        return "unknown";
    }
}

static volatile int s_pc_tenths = -1;
static volatile int s_sw_hundredths = -1;
static char s_cat_acc[48];
static size_t s_cat_acc_len;

static void cat_parse_frame(const char* frame) {
    if (!frame || frame[0] == 0) {
        return;
    }
    if (frame[0] == 'P' && frame[1] == 'C' && frame[2] >= '0' && frame[2] <= '9') {
        int v = 0;
        for (const char* p = frame + 2; *p >= '0' && *p <= '9'; ++p) {
            v = v * 10 + (*p - '0');
        }
        s_pc_tenths = v;
        return;
    }
    if (frame[0] == 'S' && frame[1] == 'W' && frame[2] >= '0' && frame[2] <= '9') {
        int v = 0;
        for (const char* p = frame + 2; *p >= '0' && *p <= '9'; ++p) {
            v = v * 10 + (*p - '0');
        }
        s_sw_hundredths = v;
    }
}

static bool cdc_data_cb(const uint8_t* data, size_t data_len, void* /*user_arg*/) {
    if (!data || data_len == 0) {
        return true;
    }
    for (size_t i = 0; i < data_len; ++i) {
        const char c = static_cast<char>(data[i]);
        if (c == ';') {
            s_cat_acc[s_cat_acc_len] = 0;
            cat_parse_frame(s_cat_acc);
            s_cat_acc_len = 0;
            continue;
        }
        if (c < 32 || c > 126) {
            continue;
        }
        if (s_cat_acc_len + 1 < sizeof(s_cat_acc)) {
            s_cat_acc[s_cat_acc_len++] = c;
        } else {
            s_cat_acc_len = 0;
        }
    }
    return true;
}

void cat_cdc_reset_parsed_meters(void) {
    s_pc_tenths = -1;
    s_sw_hundredths = -1;
    s_cat_acc_len = 0;
}

bool cat_cdc_get_parsed_meters(int* pc_tenths, int* sw_hundredths) {
    const int pc = s_pc_tenths;
    const int sw = s_sw_hundredths;
    if (pc < 0 && sw < 0) {
        return false;
    }
    if (pc_tenths) {
        *pc_tenths = pc;
    }
    if (sw_hundredths) {
        *sw_hundredths = sw;
    }
    return true;
}

// CDC-ACM helpers
static void cdc_close(void) {
    if (s_cdc_handle) {
        cdc_acm_host_close(s_cdc_handle);
        s_cdc_handle = NULL;
    }
    s_cdc_iface = -1;
    s_cdc_last_attempt_ms = 0;
    s_cdc_iface_hint = -1;
    cat_cdc_reset_parsed_meters();
}

static void cdc_event_cb(const cdc_acm_host_dev_event_data_t* event, void* user_ctx) {
    (void)user_ctx;
    if (!event) return;
    switch (event->type) {
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        ESP_LOGI(TAG, "CDC device disconnected");
        cdc_close();
        break;
    case CDC_ACM_HOST_ERROR:
        ESP_LOGW(TAG, "CDC device error: %d", event->data.error);
        break;
    default:
        break;
    }
}

static void cdc_try_open(void) {
    if (!s_cdc_installed) return;
    if (s_cdc_handle) return;

    // Throttle attempts
    int64_t now_ms = rtc_now_ms();
    if (s_cdc_last_attempt_ms != 0 && (now_ms - s_cdc_last_attempt_ms) < 1000) return;
    s_cdc_last_attempt_ms = now_ms;

    cdc_acm_host_device_config_t dev_cfg = {
        .connection_timeout_ms = 1000,
        .out_buffer_size = 64,
        .in_buffer_size = 64,
        .event_cb = cdc_event_cb,
        .data_cb = cdc_data_cb,
        .user_arg = NULL,
    };

    const int max_iface_scan = 12;

    // QMX profile: try known QMX CAT (VID/PID, iface 0) first.
    if (s_profile == UAC_PROFILE_QMX) {
        cdc_acm_dev_hdl_t handle = NULL;
        esp_err_t err = cdc_acm_host_open(k_qmx_vid, k_qmx_pid, 0, &dev_cfg, &handle);
        if (err == ESP_OK) {
            s_cdc_handle = handle;
            s_cdc_iface = 0;
            ESP_LOGI(TAG, "CDC-ACM opened (QMX iface 0, VID 0x%04x PID 0x%04x)", k_qmx_vid, k_qmx_pid);
            cdc_acm_host_desc_print(handle);
            g_cdc_initial_sync_pending = true;  // main loop will auto-sync VFO + RX mode
            return;
        } else if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "CDC open QMX iface 0 failed: %s", esp_err_to_name(err));
        }
    }

    // Try hinted CDC interface first (if we saw class 0x02)
    if (s_cdc_iface_hint >= 0) {
        cdc_acm_dev_hdl_t handle = NULL;
        esp_err_t err = cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID,
                                          (uint8_t)s_cdc_iface_hint, &dev_cfg, &handle);
        if (err == ESP_OK) {
            s_cdc_handle = handle;
            s_cdc_iface = s_cdc_iface_hint;
            ESP_LOGI(TAG, "CDC-ACM opened (hint iface %d)", s_cdc_iface_hint);
            cdc_acm_host_desc_print(handle);
            g_cdc_initial_sync_pending = true;  // main loop will auto-sync VFO + RX mode
            return;
        } else {
            ESP_LOGW(TAG, "CDC open hint iface %d failed: %s",
                     s_cdc_iface_hint, esp_err_to_name(err));
        }
    }

    for (int iface = 0; iface < max_iface_scan; ++iface) {
        cdc_acm_dev_hdl_t handle = NULL;
        esp_err_t err = cdc_acm_host_open(CDC_HOST_ANY_VID, CDC_HOST_ANY_PID,
                                          (uint8_t)iface, &dev_cfg, &handle);
        if (err == ESP_OK) {
            s_cdc_handle = handle;
            s_cdc_iface = iface;
            ESP_LOGI(TAG, "CDC-ACM opened (iface %d)", iface);
            cdc_acm_host_desc_print(handle);
            g_cdc_initial_sync_pending = true;  // main loop will auto-sync VFO + RX mode
            break;
        } else if (err != ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "CDC open iface %d failed: %s", iface, esp_err_to_name(err));
        }
    }

    if (!s_cdc_handle) {
        ESP_LOGD(TAG, "CDC-ACM not found yet (profile=%s attempt at %" PRId64 " ms)",
                 profile_name(s_profile), now_ms);
    }
}

static void cdc_new_dev_cb(usb_device_handle_t usb_dev) {
    usb_device_info_t info = {};
    if (usb_host_device_info(usb_dev, &info) == ESP_OK) {
        ESP_LOGI(TAG, "USB dev addr:%u speed:%d", info.dev_addr, info.speed);
    }

    const usb_device_desc_t* dev_desc = nullptr;
    if (usb_host_get_device_descriptor(usb_dev, &dev_desc) == ESP_OK && dev_desc) {
        ESP_LOGI(TAG, "USB dev attached: VID:0x%04x PID:0x%04x cfgs:%u",
                 dev_desc->idVendor, dev_desc->idProduct, dev_desc->bNumConfigurations);
    }

    const usb_config_desc_t* cfg = nullptr;
    if (usb_host_get_active_config_descriptor(usb_dev, &cfg) == ESP_OK && cfg) {
        const uint8_t* p = (const uint8_t*)cfg;
        int offset = 0;
        while (offset + 2 <= cfg->wTotalLength) {
            uint8_t len = p[offset];
            uint8_t dtype = p[offset + 1];
            if (len == 0) break;
            if (dtype == USB_B_DESCRIPTOR_TYPE_INTERFACE && len >= sizeof(usb_intf_desc_t)) {
                const usb_intf_desc_t* intf = (const usb_intf_desc_t*)(p + offset);
                ESP_LOGI(TAG, "  IF num=%u alt=%u eps=%u class=0x%02x subclass=0x%02x proto=0x%02x",
                         intf->bInterfaceNumber, intf->bAlternateSetting,
                         intf->bNumEndpoints, intf->bInterfaceClass,
                         intf->bInterfaceSubClass, intf->bInterfaceProtocol);
                if (intf->bInterfaceClass == USB_CLASS_COMM && s_cdc_iface_hint < 0) {
                    s_cdc_iface_hint = intf->bInterfaceNumber;
                    ESP_LOGI(TAG, "  -> CDC candidate iface %d", s_cdc_iface_hint);
                }
            }
            offset += len;
        }
    }
}

// UAC device callback
static void uac_device_callback(uac_host_device_handle_t handle,
                                 const uac_host_device_event_t event,
                                 void* arg) {
    if (event == UAC_HOST_DRIVER_EVENT_DISCONNECTED) {
        ESP_LOGI(TAG, "UAC device disconnected");
        cdc_close();
        if (handle == s_mic_handle) {
            s_mic_handle = NULL;
            s_state = UAC_STATE_WAITING;
            snprintf(s_status_string, sizeof(s_status_string), "Disconnected");
        }
        uac_host_device_close(handle);
        return;
    }

    uac_event_t evt = {};
    evt.type = UAC_EVT_DEVICE;
    evt.device.handle = handle;
    evt.device.event = event;
    xQueueSend(s_event_queue, &evt, 0);
}

// UAC driver callback
static void uac_driver_callback(uint8_t addr, uint8_t iface_num,
                                 const uac_host_driver_event_t event,
                                 void* arg) {
    ESP_LOGI(TAG, "UAC driver callback - addr:%d, iface:%d, event:%d", addr, iface_num, event);

    uac_event_t evt = {};
    evt.type = UAC_EVT_DRIVER;
    evt.driver.addr = addr;
    evt.driver.iface_num = iface_num;
    evt.driver.event = event;
    xQueueSend(s_event_queue, &evt, 0);
}

// USB host library task
static void usb_lib_task(void* arg) {
    usb_host_config_t host_config = {};
    host_config.skip_phy_setup = false;
    host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;

    if (s_profile == UAC_PROFILE_QMX) {
        // Custom FIFO partitioning enables simultaneous bidirectional ISO
        // streaming with QDX or QMX hardware. ESP32-S3 has 200 FIFO lines.
        // Built-in Kconfig biases do not cover 24-bit/48k/stereo MPS=300
        // in both directions at once. This split reserves 364 B for RX,
        // 364 B for PTX, and 72 B for non-periodic OUT (CDC CAT bulk-OUT).
        host_config.fifo_settings_custom.rx_fifo_lines   = 91;   // 364 B
        host_config.fifo_settings_custom.nptx_fifo_lines = 18;   // 72 B
        host_config.fifo_settings_custom.ptx_fifo_lines  = 91;   // 364 B
        ESP_LOGI(TAG, "USB FIFO profile=%s policy=qmx-custom rx=91 nptx=18 ptx=91",
                 profile_name(s_profile));
    } else {
        ESP_LOGI(TAG, "USB FIFO profile=%s policy=default", profile_name(s_profile));
    }

    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install USB host: %s", esp_err_to_name(err));
        s_state = UAC_STATE_ERROR;
        snprintf(s_status_string, sizeof(s_status_string), "USB init failed");
        vTaskDelete(NULL);
        return;
    }

    s_host_installed = true;
    ESP_LOGI(TAG, "USB Host installed");

    if (s_profile == UAC_PROFILE_QMX) {
        // Install CDC-ACM driver for QMX/QDX CAT control before starting class drivers.
        const cdc_acm_host_driver_config_t cdc_cfg = {
            .driver_task_stack_size = 3072,
            .driver_task_priority = 4,
            .xCoreID = 0,
            .new_dev_cb = cdc_new_dev_cb,
        };
        err = cdc_acm_host_install(&cdc_cfg);
        if (err == ESP_OK) {
            s_cdc_installed = true;
            ESP_LOGI(TAG, "CDC-ACM driver installed");
        } else {
            ESP_LOGW(TAG, "CDC-ACM driver install failed: %s", esp_err_to_name(err));
        }
    } else {
        ESP_LOGI(TAG, "CDC-ACM driver skipped profile=%s", profile_name(s_profile));
    }

    xTaskNotifyGive((TaskHandle_t)arg);

    while (!s_stop_requested) {
        uint32_t event_flags;
        err = usb_host_lib_handle_events(pdMS_TO_TICKS(100), &event_flags);
        if (err == ESP_OK) {
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
                ESP_LOGI(TAG, "No USB clients");
                usb_host_device_free_all();
            }
        }
    }

    if (s_cdc_installed) {
        cdc_close();
        cdc_acm_host_uninstall();
        s_cdc_installed = false;
        ESP_LOGI(TAG, "CDC-ACM driver uninstalled");
    }

    // Drain pending USB host events before uninstall. usb_host_uninstall()
    // refuses to release the PHY while client-detach/all-free events are
    // still pending, which blocks the subsequent TinyUSB device-mode MSC path.
    for (int i = 0; i < 50; ++i) {
        uint32_t event_flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(20), &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            break;
        }
    }

    ESP_LOGI(TAG, "USB Host uninstalling");
    esp_err_t uerr = ESP_FAIL;
    for (int attempt = 0; attempt < 20; ++attempt) {
        uint32_t event_flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(20), &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        uerr = usb_host_uninstall();
        if (uerr == ESP_OK) {
            s_host_installed = false;
            break;
        }
        ESP_LOGW(TAG, "usb_host_uninstall attempt %d: %s", attempt + 1, esp_err_to_name(uerr));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    if (uerr != ESP_OK) {
        ESP_LOGE(TAG, "USB host still owns the PHY; TinyUSB gadget will not enumerate");
    }
    s_usb_task_handle = NULL;
    vTaskDelete(NULL);
}

// UAC class driver task
static void uac_lib_task(void* arg) {
    // Wait for USB host to be ready
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    uac_host_driver_config_t uac_config = {
        .create_background_task = true,
        .task_priority = UAC_TASK_PRIORITY,
        .stack_size = 4096,
        .core_id = 0,
        .callback = uac_driver_callback,
        .callback_arg = NULL
    };

    esp_err_t err = uac_host_install(&uac_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UAC driver: %s", esp_err_to_name(err));
        s_state = UAC_STATE_ERROR;
        snprintf(s_status_string, sizeof(s_status_string), "UAC init failed");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UAC driver installed");
    s_state = UAC_STATE_WAITING;
    snprintf(s_status_string, sizeof(s_status_string), "Waiting for device");

    uac_event_t evt;
    while (!s_stop_requested) {
        if (xQueueReceive(s_event_queue, &evt, pdMS_TO_TICKS(100))) {
            if (evt.type == UAC_EVT_STOP) {
                break;
            } else if (evt.type == UAC_EVT_DRIVER) {
                if (evt.driver.event == UAC_HOST_DRIVER_EVENT_RX_CONNECTED) {
                    ESP_LOGI(TAG, "Microphone connected - addr:%d, iface:%d",
                             evt.driver.addr, evt.driver.iface_num);

                    if (s_mic_handle != NULL) {
                        ESP_LOGW(TAG, "Already have a mic device, ignoring");
                        continue;
                    }

                    uac_host_device_config_t dev_config = {
                        .addr = evt.driver.addr,
                        .iface_num = evt.driver.iface_num,
                        .buffer_size = UAC_BUFFER_SIZE,
                        .buffer_threshold = UAC_BUFFER_THRESHOLD,
                        .callback = uac_device_callback,
                        .callback_arg = NULL
                    };

                    uac_host_device_handle_t handle = NULL;
                    err = uac_host_device_open(&dev_config, &handle);
                    if (err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to open device: %s", esp_err_to_name(err));
                        snprintf(s_status_string, sizeof(s_status_string), "Open failed");
                        continue;
                    }

                    // Print device info
                    uac_host_printf_device_param(handle);

                    // Start stream format negotiation.
                    // QMX profile keeps the existing strict format.
                    // Generic profile stays 48 kHz and tries mic-only
                    // mono/stereo 24-bit and 16-bit candidates.
                    uac_host_stream_config_t candidates[4];
                    int candidate_count = 0;
                    auto add_candidate = [&](uint8_t ch, uint8_t bits) {
                        candidates[candidate_count].channels = ch;
                        candidates[candidate_count].bit_resolution = bits;
                        candidates[candidate_count].sample_freq = UAC_SAMPLE_RATE;
                        candidates[candidate_count].flags = 0;
                        candidate_count++;
                    };
                    if (s_profile == UAC_PROFILE_GENERIC_USB) {
                        add_candidate(1, 24);
                        add_candidate(1, 16);
                        add_candidate(2, 24);
                        add_candidate(2, 16);
                    } else {
                        add_candidate(UAC_CHANNELS, UAC_BIT_RESOLUTION);
                    }

                    bool started = false;
                    for (int i = 0; i < candidate_count; ++i) {
                        const uac_host_stream_config_t* cfg = &candidates[i];
                        ESP_LOGI(TAG, "Starting stream (profile=%s) candidate %d/%d: %dHz, %d-bit, %dch",
                                 profile_name(s_profile), i + 1, candidate_count,
                                 cfg->sample_freq, cfg->bit_resolution, cfg->channels);
                        err = uac_host_device_start(handle, cfg);
                        if (err == ESP_OK) {
                            s_format.sample_freq = cfg->sample_freq;
                            s_format.bit_resolution = cfg->bit_resolution;
                            s_format.channels = cfg->channels;
                            started = true;
                            ESP_LOGI(TAG, "Selected stream format: %dHz, %d-bit, %dch",
                                     s_format.sample_freq, s_format.bit_resolution, s_format.channels);
                            break;
                        }
                        ESP_LOGW(TAG, "Stream candidate failed: %s", esp_err_to_name(err));
                    }

                    if (!started) {
                        if (s_profile == UAC_PROFILE_GENERIC_USB) {
                            ESP_LOGE(TAG, "No 48k UAC mic format for profile=%s",
                                     profile_name(s_profile));
                            snprintf(s_status_string, sizeof(s_status_string), "No 48k UAC mic format");
                        } else {
                            ESP_LOGE(TAG, "Failed to start stream for profile=%s",
                                     profile_name(s_profile));
                            snprintf(s_status_string, sizeof(s_status_string), "Format not supported");
                        }
                        uac_host_device_close(handle);
                        continue;
                    }

                    s_mic_handle = handle;
                    s_state = UAC_STATE_STREAMING;
                    g_streaming = true;
                    snprintf(s_status_string, sizeof(s_status_string),
                             "Streaming %s %luk/%u/%u",
                             profile_name(s_profile),
                             (unsigned long)(s_format.sample_freq / 1000),
                             s_format.bit_resolution,
                             s_format.channels);

                    if (s_profile == UAC_PROFILE_QMX) {
                        // Try to open companion CDC-ACM interface (CAT).
                        cdc_try_open();
                    }

                    // Start the audio processing task.
                    //
                    // FT4's decoder needs a larger stack than FT8. Keep the
                    // FT8 stack static and allocate the FT4 stack on demand.
                    if (s_stream_task_handle == NULL) {
#if ENABLE_FT4
                        if (g_protocol == &kProtocolFT4) {
                            BaseType_t cr = xTaskCreatePinnedToCore(
                                stream_uac_task, "stream_uac",
                                16384, NULL,
                                UAC_STREAM_TASK_PRIORITY,
                                &s_stream_task_handle, 1);
                            if (cr != pdPASS) {
                                ESP_LOGE(TAG, "stream_uac_task create FAILED (dynamic FT4) free=%u",
                                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
                                s_stream_task_handle = NULL;
                            }
                        } else {
#endif
                        // FT8 uses the existing static 8 KB stack.
                        static StackType_t  s_stream_task_stack[8192 / sizeof(StackType_t)];
                        static StaticTask_t s_stream_task_tcb;
                        size_t free_before = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
                        size_t largest = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
                        ESP_LOGI(TAG, "Pre-task-create heap: free=%u largest=%u",
                                 (unsigned)free_before, (unsigned)largest);
                        s_stream_task_handle = xTaskCreateStaticPinnedToCore(
                            stream_uac_task, "stream_uac",
                            8192 / sizeof(StackType_t), NULL,
                            UAC_STREAM_TASK_PRIORITY,
                            s_stream_task_stack, &s_stream_task_tcb, 1);
                        if (!s_stream_task_handle) {
                            ESP_LOGE(TAG, "stream_uac_task create FAILED "
                                     "(static FT8) free=%u largest=%u",
                                     (unsigned)free_before, (unsigned)largest);
                        }
#if ENABLE_FT4
                        }
#endif
                    }

                } else if (evt.driver.event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED) {
                    if (s_profile != UAC_PROFILE_QMX) {
                        ESP_LOGI(TAG, "Speaker connected ignored profile=%s addr=%u iface=%u",
                                 profile_name(s_profile),
                                 (unsigned)evt.driver.addr,
                                 (unsigned)evt.driver.iface_num);
                        continue;
                    }

                    s_spk_addr  = evt.driver.addr;
                    s_spk_iface = evt.driver.iface_num;
                    s_spk_known = true;
                    ESP_LOGI(TAG, "Speaker captured: addr=%u iface=%u",
                             (unsigned)s_spk_addr, (unsigned)s_spk_iface);
                    // Open and claim the speaker while enumeration-time heap
                    // is contiguous, then hold it across all TX cycles.
                    if (!s_spk_handle) {
                        uac_host_device_config_t cfg = {};
                        cfg.addr = s_spk_addr;
                        cfg.iface_num = s_spk_iface;
                        cfg.buffer_size = SPK_BUFFER_SIZE;
                        cfg.buffer_threshold = SPK_BUFFER_THRESHOLD;
                        cfg.callback = spk_event_cb;
                        cfg.callback_arg = nullptr;
                        esp_err_t oerr = uac_host_device_open(&cfg, &s_spk_handle);
                        if (oerr != ESP_OK) {
                            ESP_LOGE(TAG, "spk pre-open failed at enum: %s",
                                     esp_err_to_name(oerr));
                            s_spk_handle = NULL;
                        } else {
                            // Claim iface + alloc xfer URBs + EP slot now,
                            // but pass FLAG_STREAM_SUSPEND_AFTER_START so
                            // no URBs are queued on the bus yet. The
                            // 512-byte aligned xfer_desc_list (2x 512B
                            // for ISOC double-buffering) requires fresh
                            // heap to satisfy alignment — at TX time the
                            // largest contiguous block is <2 KB and
                            // aligned alloc fails. Resume happens at TX
                            // via uac_host_device_resume (cheap, no
                            // realloc). Suspend at TX-end stops URB flow
                            // but keeps everything allocated.
                            uac_host_stream_config_t scfg = {};
                            scfg.channels = 2;
                            scfg.bit_resolution = 24;
                            scfg.sample_freq = 48000;
                            scfg.flags = FLAG_STREAM_SUSPEND_AFTER_START;
                            esp_err_t serr = uac_host_device_start(s_spk_handle, &scfg);
                            if (serr != ESP_OK) {
                                ESP_LOGE(TAG, "spk pre-start failed at enum: %s",
                                         esp_err_to_name(serr));
                                uac_host_device_close(s_spk_handle);
                                s_spk_handle = NULL;
                            } else {
                                ESP_LOGI(TAG, "Speaker pre-opened+claimed (handle=%p, ringbuf=%u B, suspended)",
                                         s_spk_handle, (unsigned)SPK_BUFFER_SIZE);
                                // Allocate the pump buffer + permanent
                                // writer task NOW too, while heap is
                                // healthy. Per-TX cost is just flipping
                                // s_spk_writer_run — no allocs.
                                if (!s_spk_pump_buf) {
                                    s_spk_pump_buf = (uint8_t*)heap_caps_malloc(
                                        SPK_PUMP_BYTES, MALLOC_CAP_DEFAULT);
                                    if (!s_spk_pump_buf) {
                                        ESP_LOGE(TAG, "spk pump buf alloc failed at enum");
                                    }
                                }
                                if (s_spk_pump_buf && !s_spk_writer_task) {
                                    s_spk_writer_stop = false;
                                    s_spk_writer_run = false;
                                    BaseType_t ok = xTaskCreatePinnedToCore(
                                        spk_writer_task, "uac_tx_writer",
                                        3072, NULL, 5, &s_spk_writer_task, 1);
                                    if (ok != pdPASS) {
                                        ESP_LOGE(TAG, "spk writer task create failed at enum");
                                        s_spk_writer_task = NULL;
                                    } else {
                                        ESP_LOGI(TAG, "Speaker writer task ready (idle)");
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Cleanup
    if (s_mic_handle) {
        uac_host_device_stop(s_mic_handle);
        uac_host_device_close(s_mic_handle);
        s_mic_handle = NULL;
    }
    // Stop the permanent writer task so it exits its loop and frees
    // its TCB before we tear down the stream / pump buffer.
    if (s_spk_writer_task) {
        s_spk_writer_run = false;
        s_spk_writer_stop = true;
        for (int i = 0; i < 50 && s_spk_writer_task; ++i) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    if (s_spk_pump_buf) {
        free(s_spk_pump_buf);
        s_spk_pump_buf = nullptr;
    }
    if (s_spk_handle) {
        // Speaker was opened + claimed (suspended) at TX_CONNECTED
        // enum and held for the lifetime of the host driver. Tear it
        // down here before uac_host_uninstall() to release xfer URBs
        // and ringbuffer cleanly.
        uac_host_device_stop(s_spk_handle);
        uac_host_device_close(s_spk_handle);
        s_spk_handle = NULL;
        s_spk_known = false;
    }

    ESP_LOGI(TAG, "UAC driver uninstalling");
    uac_host_uninstall();
    s_uac_task_handle = NULL;
    vTaskDelete(NULL);
}

// Audio streaming and processing task
static void stream_uac_task(void* arg) {
    (void)arg;
    ESP_LOGI(TAG, "Audio streaming task started");

    resample_init(&s_resample_state);
    uint8_t* usb_buffer = s_usb_buffer;

    ft8_audio_pipeline_config_t pipe_cfg = {
        .tag = TAG,
        .ctx = usb_buffer,
        .read = uac_read_ft8_samples,
        .should_stop = uac_should_stop,
        .on_block_processed = uac_on_block_processed,
    };
    ft8_audio_pipeline_run(&pipe_cfg);

    g_streaming = false;
    s_stream_task_handle = NULL;
    ESP_LOGI(TAG, "Audio streaming task stopped");
    vTaskDelete(NULL);
}

static int uac_read_ft8_samples(void* ctx, float* out, int max_samples) {
    (void)max_samples;
    uint8_t* usb_buffer = (uint8_t*)ctx;
    if (!usb_buffer || !out) return 0;

    while (!s_stop_requested && s_mic_handle != NULL) {
        uint32_t bytes_read = 0;
        esp_err_t ret = uac_host_device_read(s_mic_handle, usb_buffer,
                                              UAC_READ_BUFFER_SIZE,
                                              &bytes_read,
                                              pdMS_TO_TICKS(200));
        if (ret != ESP_OK || bytes_read == 0) {
            if (ret != ESP_ERR_TIMEOUT && ret != ESP_FAIL) {
                ESP_LOGW(TAG, "USB read error: %s", esp_err_to_name(ret));
            }
            continue;
        }

        int frame_bytes = (s_format.bit_resolution / 8) * s_format.channels;
        if (frame_bytes <= 0) {
            ESP_LOGW(TAG, "Invalid stream format bytes/frame=%d", frame_bytes);
            continue;
        }
        int num_frames = bytes_read / frame_bytes;
        int remainder = bytes_read % frame_bytes;
        if (num_frames == 0) continue;

        int32_t val = 0;
        if (s_format.bit_resolution == 24) {
            val = usb_buffer[0] | (usb_buffer[1] << 8) | (usb_buffer[2] << 16);
            if (val & 0x800000) val |= 0xFF000000;
        } else {
            int16_t v16 = (int16_t)(usb_buffer[0] | (usb_buffer[1] << 8));
            val = v16;
        }
        snprintf(s_debug_line1, sizeof(s_debug_line1),
                 "fmt=%lu/%u/%u v=%ld",
                 (unsigned long)s_format.sample_freq,
                 s_format.bit_resolution,
                 s_format.channels,
                 (long)val);
        snprintf(s_debug_line2, sizeof(s_debug_line2),
                 "rd=%lu fb=%d rem=%d", (unsigned long)bytes_read, frame_bytes, remainder);

        return uac_pcm_to_ft8_samples(&s_resample_state, usb_buffer,
                                      (int)bytes_read, out,
                                      s_format.bit_resolution,
                                      s_format.channels);
    }
    return 0;
}

static bool uac_should_stop(void* ctx) {
    (void)ctx;
    return s_stop_requested || s_mic_handle == NULL;
}

static void uac_on_block_processed(void* ctx) {
    (void)ctx;
    if (!s_cdc_handle) {
        cdc_try_open();
    }
}

// Public API implementation
uac_stream_state_t uac_get_state(void) {
    return s_state;
}

bool uac_is_streaming(void) {
    return s_state == UAC_STATE_STREAMING && s_mic_handle != NULL;
}

bool uac_get_latest_waterfall_row(uint8_t* out_row, int out_len) {
    return ft8_audio_pipeline_get_latest_waterfall_row(out_row, out_len);
}

bool uac_start_with_profile(uac_stream_profile_t profile) {
    if (s_state != UAC_STATE_IDLE) {
        ESP_LOGW(TAG, "UAC already started");
        return false;
    }

    s_profile = profile;
    s_cdc_last_attempt_ms = 0;
    s_cdc_iface = -1;
    s_cdc_iface_hint = -1;
    s_format.sample_freq = UAC_SAMPLE_RATE;
    s_format.bit_resolution = UAC_BIT_RESOLUTION;
    s_format.channels = UAC_CHANNELS;
    ft8_audio_pipeline_clear_latest_waterfall_row();

    ESP_LOGI(TAG, "Starting UAC host profile=%s", profile_name(s_profile));
    s_stop_requested = false;
    resample_init(&s_resample_state);

    // Create event queue
    s_event_queue = xQueueCreate(10, sizeof(uac_event_t));
    if (!s_event_queue) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return false;
    }

    // Create UAC task first (it will wait for USB task notification)
    BaseType_t ret = xTaskCreatePinnedToCore(uac_lib_task, "uac_lib",
                                              TASK_STACK_SIZE, NULL,
                                              UAC_TASK_PRIORITY,
                                              &s_uac_task_handle, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UAC task");
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return false;
    }

    // Create USB host task (will notify UAC task when ready)
    ret = xTaskCreatePinnedToCore(usb_lib_task, "usb_lib",
                                   TASK_STACK_SIZE, (void*)s_uac_task_handle,
                                   USB_HOST_TASK_PRIORITY,
                                   &s_usb_task_handle, 0);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create USB task");
        s_stop_requested = true;
        vTaskDelete(s_uac_task_handle);
        s_uac_task_handle = NULL;
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
        return false;
    }

    s_state = UAC_STATE_WAITING;
    snprintf(s_status_string, sizeof(s_status_string), "Waiting for %s", profile_name(s_profile));
    return true;
}

bool uac_start(void) {
    return uac_start_with_profile(UAC_PROFILE_QMX);
}

bool uac_qmx_detected(void) {
  return s_mic_handle != NULL || s_cdc_handle != NULL;
}

void uac_stop(void) {
    if (s_state == UAC_STATE_IDLE) {
        return;
    }

    ESP_LOGI(TAG, "Stopping UAC host");
    s_stop_requested = true;
    g_streaming = false;
    cdc_close();

    // Send stop event
    if (s_event_queue) {
        uac_event_t evt = {};
        evt.type = UAC_EVT_STOP;
        xQueueSend(s_event_queue, &evt, pdMS_TO_TICKS(100));
    }

    // Wait for tasks to finish
    int timeout = 50;  // 5 seconds
    while ((s_stream_task_handle || s_uac_task_handle || s_usb_task_handle) && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout--;
    }

    if (s_event_queue) {
        vQueueDelete(s_event_queue);
        s_event_queue = NULL;
    }

    s_state = UAC_STATE_IDLE;
    ft8_audio_pipeline_clear_latest_waterfall_row();
    snprintf(s_status_string, sizeof(s_status_string), "Idle");
    ESP_LOGI(TAG, "UAC host stopped");
}

bool uac_usb_host_released(void) {
    return !s_host_installed && s_usb_task_handle == NULL;
}

esp_err_t uac_ensure_host_uninstalled(void) {
    uac_stop();

    int timeout = 50;
    while (s_usb_task_handle != NULL && timeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        timeout--;
    }
    if (s_usb_task_handle != NULL) {
        ESP_LOGE(TAG, "USB host task still running; cannot start USB Drive");
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_host_installed) {
        return ESP_OK;
    }

    for (int attempt = 0; attempt < 20; ++attempt) {
        uint32_t event_flags = 0;
        usb_host_lib_handle_events(pdMS_TO_TICKS(20), &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
        const esp_err_t uerr = usb_host_uninstall();
        if (uerr == ESP_OK) {
            s_host_installed = false;
            ESP_LOGI(TAG, "USB host released for device-mode MSC");
            return ESP_OK;
        }
        ESP_LOGW(TAG, "USB host release attempt %d: %s", attempt + 1, esp_err_to_name(uerr));
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return ESP_ERR_INVALID_STATE;
}

// UAC OUT synthesis for QDX mode.

static void spk_event_cb(uac_host_device_handle_t /*dev*/,
                         const uac_host_device_event_t event,
                         void* /*arg*/) {
    if (event == UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR) {
        s_spk_write_errors++;
        ESP_LOGW(TAG, "spk transfer error");
    }
}

static void spk_writer_task(void* /*arg*/) {
    // Permanent task, gated per transmission. The DDS owns symbol timing.
    const uint32_t frames_per_block = SPK_PUMP_BYTES / SPK_FRAME_BYTES;  // 2304
    while (!s_spk_writer_stop) {
        if (!s_spk_writer_run) {
            // Idle between TX cycles. Sleep cheaply.
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        dds_render_24bit_stereo(s_spk_pump_buf, frames_per_block);
        esp_err_t err = uac_host_device_write(s_spk_handle, s_spk_pump_buf, SPK_PUMP_BYTES,
                                              pdMS_TO_TICKS(SPK_WRITE_TIMEOUT_MS));
        if (err == ESP_OK) {
            s_spk_packets_sent += SPK_PUMP_PACKETS;
        } else if (err != ESP_ERR_TIMEOUT) {
            s_spk_write_errors++;
            if (s_spk_write_errors <= 5) {
                ESP_LOGW(TAG, "spk write err=%s", esp_err_to_name(err));
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        // Yield so IDLE0 can run (watchdog appeasement).
        vTaskDelay(1);
    }

    s_spk_writer_task = NULL;
    vTaskDelete(NULL);
}

static bool uac_tx_start_writer(void) {
    if (s_spk_writer_run) {
        ESP_LOGW(TAG, "UAC OUT already active");
        return false;
    }
    if (!s_spk_known || !s_spk_handle || !s_spk_writer_task) {
        ESP_LOGW(TAG, "UAC OUT speaker not ready");
        return false;
    }

    s_spk_packets_sent = 0;
    s_spk_write_errors = 0;

    esp_err_t err = uac_host_device_resume(s_spk_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "UAC OUT resume failed: %s", esp_err_to_name(err));
        return false;
    }

    s_spk_writer_run = true;
    return true;
}

bool uac_tx_begin_cpfsk(float base_hz,
                        const uint8_t* symbols,
                        size_t symbol_count,
                        float tone_spacing_hz,
                        uint32_t samples_per_symbol) {
    if (!dds_cpfsk_begin(static_cast<double>(base_hz), symbols, symbol_count,
                         static_cast<double>(tone_spacing_hz),
                         samples_per_symbol)) {
        ESP_LOGE(TAG, "Invalid CPFSK schedule count=%u samples/symbol=%u",
                 (unsigned)symbol_count, (unsigned)samples_per_symbol);
        return false;
    }

    if (!uac_tx_start_writer()) {
        dds_cpfsk_end();
        return false;
    }

    ESP_LOGI(TAG,
             "UAC OUT CPFSK start base=%.2f count=%u spacing=%.4f samples/symbol=%u",
             (double)base_hz, (unsigned)symbol_count, (double)tone_spacing_hz,
             (unsigned)samples_per_symbol);
    return true;
}

bool uac_tx_begin_tune(float tone_hz) {
    dds_cpfsk_end();
    dds_reset_phase();
    dds_set_freq_hz(static_cast<double>(tone_hz));
    if (!uac_tx_start_writer()) {
        dds_set_freq_hz(0.0);
        return false;
    }

    ESP_LOGI(TAG, "UAC OUT tune start tone=%.2f", (double)tone_hz);
    return true;
}

void uac_tx_end(void) {
    if (!s_spk_writer_run) {
        dds_cpfsk_end();
        dds_set_freq_hz(0.0);
        return;
    }

    s_spk_writer_run = false;
    vTaskDelay(pdMS_TO_TICKS(20));
    dds_cpfsk_end();
    dds_set_freq_hz(0.0);

    if (s_spk_handle) {
        uac_host_device_suspend(s_spk_handle);
    }

    ESP_LOGI(TAG, "UAC OUT stopped packets=%u errors=%u",
             (unsigned)s_spk_packets_sent, (unsigned)s_spk_write_errors);
}

const char* uac_get_status_string(void) {
    return s_status_string;
}

const char* uac_get_debug_line1(void) {
    return s_debug_line1;
}

const char* uac_get_debug_line2(void) {
    return s_debug_line2;
}

bool cat_cdc_ready(void) {
    return s_cdc_handle != NULL;
}

esp_err_t cat_cdc_send(const uint8_t* data, size_t len, uint32_t timeout_ms) {
    if (!s_cdc_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    return cdc_acm_host_data_tx_blocking(s_cdc_handle, data, len, timeout_ms);
}

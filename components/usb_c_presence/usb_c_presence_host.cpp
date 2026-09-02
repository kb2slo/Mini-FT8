#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb/usb_host.h"
#include "usb/usb_helpers.h"
#include "usb/usb_types_ch9.h"

#include "usb_c_presence.h"

static constexpr uint8_t kUacSubclassAudioStreaming = 0x02;

static const char* TAG = "USB_C";

static usb_host_client_handle_t s_client = nullptr;
static TaskHandle_t s_task = nullptr;
static QueueHandle_t s_events = nullptr;
static volatile bool s_stop = false;
static volatile bool s_notify = true;
static usb_device_handle_t s_dev = nullptr;
static usb_device_handle_t s_close_hdl = nullptr;
static UsbCDevice s_last = {UsbCKind::Other, 0, 0};
static bool s_have_last = false;

static void post_event(UsbCPresenceAction action, const UsbCDevice& device)
{
    if (!s_events || !s_notify) {
        return;
    }
    UsbCPresenceEvent ev = {};
    ev.action = action;
    ev.device = device;
    (void)xQueueSend(s_events, &ev, 0);
}

static void client_event_cb(const usb_host_client_event_msg_t* event_msg, void* /*arg*/)
{
    if (!event_msg) {
        return;
    }
    switch (event_msg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV: {
        usb_device_handle_t dev = nullptr;
        if (usb_host_device_open(s_client, event_msg->new_dev.address, &dev) != ESP_OK || !dev) {
            ESP_LOGW(TAG, "open addr %u failed", event_msg->new_dev.address);
            return;
        }
        const usb_device_desc_t* desc = nullptr;
        UsbCDevice device = {};
        if (usb_host_get_device_descriptor(dev, &desc) == ESP_OK && desc) {
            device.vid = desc->idVendor;
            device.pid = desc->idProduct;
            device.kind = usb_c_classify(device.vid, device.pid);
        } else {
            device.kind = UsbCKind::Other;
        }
        if (s_dev) {
            (void)usb_host_device_close(s_client, s_dev);
        }
        s_dev = dev;
        s_last = device;
        s_have_last = true;
        ESP_LOGI(TAG, "attach VID:0x%04x PID:0x%04x kind=%u", device.vid, device.pid,
                 static_cast<unsigned>(device.kind));
        post_event(UsbCPresenceAction::Attach, device);
        break;
    }
    case USB_HOST_CLIENT_EVENT_DEV_GONE: {
        UsbCDevice gone = s_have_last ? s_last : UsbCDevice{UsbCKind::Other, 0, 0};
        // Close after handle_events returns. The hub port is not recycled
        // until every client that opened the device closes it; skipping
        // that blocks the next insertion.
        s_close_hdl = event_msg->dev_gone.dev_hdl;
        s_dev = nullptr;
        s_have_last = false;
        ESP_LOGI(TAG, "detach VID:0x%04x PID:0x%04x", gone.vid, gone.pid);
        post_event(UsbCPresenceAction::Detach, gone);
        break;
    }
    default:
        break;
    }
}

static void presence_client_task(void* /*arg*/)
{
    while (!s_stop && s_client) {
        (void)usb_host_client_handle_events(s_client, pdMS_TO_TICKS(100));
        if (s_close_hdl && s_client) {
            const esp_err_t cerr = usb_host_device_close(s_client, s_close_hdl);
            if (cerr != ESP_OK) {
                ESP_LOGW(TAG, "close gone device: %s", esp_err_to_name(cerr));
            }
            s_close_hdl = nullptr;
        }
    }
    s_task = nullptr;
    vTaskDelete(NULL);
}

esp_err_t usb_c_presence_start(void)
{
    if (s_client) {
        return ESP_OK;
    }
    s_stop = false;
    s_dev = nullptr;
    s_close_hdl = nullptr;
    s_have_last = false;
    if (!s_events) {
        s_events = xQueueCreate(4, sizeof(UsbCPresenceEvent));
        if (!s_events) {
            return ESP_ERR_NO_MEM;
        }
    } else {
        xQueueReset(s_events);
    }

    const usb_host_client_config_t cfg = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = client_event_cb,
            .callback_arg = nullptr,
        },
    };
    esp_err_t err = usb_host_client_register(&cfg, &s_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "client register: %s", esp_err_to_name(err));
        return err;
    }

    BaseType_t ok = xTaskCreatePinnedToCore(presence_client_task, "usb_c", 3072, nullptr, 4, &s_task, 0);
    if (ok != pdPASS) {
        (void)usb_host_client_deregister(s_client);
        s_client = nullptr;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "presence client started");
    return ESP_OK;
}

void usb_c_presence_stop(void)
{
    s_stop = true;
    for (int i = 0; i < 20 && s_task != nullptr; ++i) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_client && s_dev) {
        (void)usb_host_device_close(s_client, s_dev);
        s_dev = nullptr;
    }
    if (s_client && s_close_hdl) {
        (void)usb_host_device_close(s_client, s_close_hdl);
        s_close_hdl = nullptr;
    }
    if (s_client) {
        (void)usb_host_client_deregister(s_client);
        s_client = nullptr;
    }
    s_have_last = false;
    if (s_events) {
        xQueueReset(s_events);
    }
    ESP_LOGI(TAG, "presence client stopped");
}

void usb_c_presence_set_notify(bool enabled)
{
    s_notify = enabled;
    if (s_events) {
        xQueueReset(s_events);
    }
}

bool usb_c_presence_take_event(UsbCPresenceEvent* out)
{
    if (!out || !s_events) {
        return false;
    }
    return xQueueReceive(s_events, out, 0) == pdTRUE;
}

void usb_c_presence_yield_device(void)
{
    if (!s_client || !s_dev) {
        return;
    }
    (void)usb_host_device_close(s_client, s_dev);
    s_dev = nullptr;
    ESP_LOGI(TAG, "yield device for class drivers");
}

static void probe_uac_ifaces_on_config(uint8_t addr, const usb_config_desc_t* cfg, usb_c_uac_iface_cb cb,
                                       void* arg)
{
    if (!cfg || !cb) {
        return;
    }
    const uint16_t total_length = cfg->wTotalLength;
    int iface_offset = 0;
    const usb_intf_desc_t* iface_desc =
        (const usb_intf_desc_t*)usb_parse_next_descriptor_of_type((const usb_standard_desc_t*)cfg, total_length,
                                                                  USB_B_DESCRIPTOR_TYPE_INTERFACE, &iface_offset);
    while (iface_desc != nullptr) {
        if (iface_desc->bInterfaceClass == USB_CLASS_AUDIO &&
            iface_desc->bInterfaceSubClass == kUacSubclassAudioStreaming) {
            const usb_intf_desc_t* iface_alt_desc =
                (const usb_intf_desc_t*)usb_parse_next_descriptor((const usb_standard_desc_t*)iface_desc,
                                                                    total_length, &iface_offset);
            int ep_offset = iface_offset;
            const usb_ep_desc_t* ep_desc =
                usb_parse_endpoint_descriptor_by_index(iface_alt_desc, 0, total_length, &ep_offset);
            if (ep_desc != nullptr) {
                const bool is_rx = (ep_desc->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) != 0;
                cb(addr, iface_desc->bInterfaceNumber, is_rx, arg);
            }
            while (iface_alt_desc != nullptr) {
                if (iface_alt_desc->bInterfaceNumber != iface_desc->bInterfaceNumber) {
                    break;
                }
                iface_alt_desc = (const usb_intf_desc_t*)usb_parse_next_descriptor(
                    (const usb_standard_desc_t*)iface_alt_desc, total_length, &iface_offset);
            }
            iface_desc = iface_alt_desc;
            continue;
        }
        iface_desc = (const usb_intf_desc_t*)usb_parse_next_descriptor((const usb_standard_desc_t*)iface_desc,
                                                                       total_length, &iface_offset);
    }
}

esp_err_t usb_c_presence_probe_uac_interfaces(usb_c_uac_iface_cb cb, void* arg)
{
    if (!cb || !s_client) {
        return ESP_ERR_INVALID_STATE;
    }
    usb_c_presence_yield_device();

    uint8_t addrs[4] = {};
    int count = 0;
    esp_err_t err = usb_host_device_addr_list_fill((int)sizeof(addrs), addrs, &count);
    if (err != ESP_OK || count == 0) {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "probing %d device(s) for UAC (no bus reset)", count);
    for (int i = 0; i < count; ++i) {
        usb_device_handle_t dev = nullptr;
        err = usb_host_device_open(s_client, addrs[i], &dev);
        if (err != ESP_OK || !dev) {
            ESP_LOGW(TAG, "probe open addr %u: %s", addrs[i], esp_err_to_name(err));
            continue;
        }
        const usb_config_desc_t* cfg = nullptr;
        if (usb_host_get_active_config_descriptor(dev, &cfg) == ESP_OK && cfg) {
            probe_uac_ifaces_on_config(addrs[i], cfg, cb, arg);
        }
        (void)usb_host_device_close(s_client, dev);
    }
    return ESP_OK;
}

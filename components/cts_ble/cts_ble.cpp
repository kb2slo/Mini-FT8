#include "cts_ble.h"

#include <cstring>

#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "cts_time.h"

static const char* TAG = "CTS_BLE";
static constexpr int64_t kAdvertiseTimeoutUs = 90LL * 1000000LL;

extern "C" void ble_store_config_init(void);

static const ble_uuid16_t k_cts_svc = BLE_UUID16_INIT(0x1805);
static const ble_uuid16_t k_cts_chr = BLE_UUID16_INIT(0x2A2B);

static CtsBleState s_state = CtsBleState::Idle;
static char s_name[20];
static char s_menu[24];
static uint16_t s_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t s_svc_start;
static uint16_t s_svc_end;
static uint16_t s_chr_val;
static bool s_have_svc;
static bool s_have_chr;
static bool s_have_result;
static bool s_ui_dirty;
static bool s_stop_host;
static bool s_host_up;
static struct timeval s_tv;
static int64_t s_deadline_us;
static uint8_t s_own_addr_type;

static void set_state(CtsBleState st, const char* menu)
{
    s_state = st;
    std::strncpy(s_menu, menu, sizeof(s_menu) - 1);
    s_menu[sizeof(s_menu) - 1] = '\0';
    s_ui_dirty = true;
}

static void request_stop(void)
{
    s_stop_host = true;
}

static void fail_now(const char* why)
{
    ESP_LOGW(TAG, "%s", why);
    set_state(CtsBleState::Failed, why);
    if (s_conn != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(s_conn, BLE_ERR_REM_USER_CONN_TERM);
    }
    request_stop();
}

static int on_read(uint16_t conn, const struct ble_gatt_error* error, struct ble_gatt_attr* attr,
                   void* /*arg*/)
{
    if (error == nullptr || error->status != 0 || attr == nullptr || attr->om == nullptr) {
        fail_now("Read fail");
        return 0;
    }

    std::uint8_t buf[kCtsCurrentTimeSize];
    if (os_mbuf_copydata(attr->om, 0, kCtsCurrentTimeSize, buf) != 0) {
        fail_now("Short CTS");
        return 0;
    }
    if (!cts_parse_current_time_to_timeval(buf, kCtsCurrentTimeSize, &s_tv)) {
        fail_now("Bad CTS");
        return 0;
    }

    s_have_result = true;
    set_state(CtsBleState::Success, "Time OK");
    ESP_LOGI(TAG, "CTS read ok epoch=%ld usec=%ld", (long)s_tv.tv_sec, (long)s_tv.tv_usec);
    ble_gap_terminate(conn, BLE_ERR_REM_USER_CONN_TERM);
    return 0;
}

static int on_chr(uint16_t conn, const struct ble_gatt_error* error, const struct ble_gatt_chr* chr,
                  void* /*arg*/)
{
    if (error != nullptr && error->status == BLE_HS_EDONE) {
        if (!s_have_chr) {
            fail_now("No 2A2B");
            return 0;
        }
        set_state(CtsBleState::Reading, "Reading...");
        int rc = ble_gattc_read(conn, s_chr_val, on_read, nullptr);
        if (rc != 0) {
            fail_now("Read start");
        }
        return 0;
    }
    if (error != nullptr && error->status != 0) {
        fail_now("Chr disc");
        return 0;
    }
    if (chr != nullptr) {
        s_chr_val = chr->val_handle;
        s_have_chr = true;
    }
    return 0;
}

static int on_svc(uint16_t conn, const struct ble_gatt_error* error, const struct ble_gatt_svc* svc,
                  void* /*arg*/)
{
    if (error != nullptr && error->status == BLE_HS_EDONE) {
        if (!s_have_svc) {
            fail_now("No 1805");
            return 0;
        }
        int rc = ble_gattc_disc_chrs_by_uuid(conn, s_svc_start, s_svc_end, &k_cts_chr.u, on_chr,
                                             nullptr);
        if (rc != 0) {
            fail_now("Chr start");
        }
        return 0;
    }
    if (error != nullptr && error->status != 0) {
        fail_now("Svc disc");
        return 0;
    }
    if (svc != nullptr) {
        s_svc_start = svc->start_handle;
        s_svc_end = svc->end_handle;
        s_have_svc = true;
    }
    return 0;
}

static void start_cts_disc(uint16_t conn)
{
    s_have_svc = false;
    s_have_chr = false;
    set_state(CtsBleState::Connected, "Paired");
    int rc = ble_gattc_disc_svc_by_uuid(conn, &k_cts_svc.u, on_svc, nullptr);
    if (rc != 0) {
        fail_now("Disc start");
    }
}

static void advertise(void);

static int gap_event(struct ble_gap_event* event, void* /*arg*/)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status != 0) {
                advertise();
                return 0;
            }
            s_conn = event->connect.conn_handle;
            set_state(CtsBleState::Connected, "Connected");
            {
                struct ble_gap_conn_desc desc;
                if (ble_gap_conn_find(s_conn, &desc) == 0 && desc.sec_state.encrypted) {
                    start_cts_disc(s_conn);
                } else {
                    int sec = ble_gap_security_initiate(s_conn);
                    if (sec != 0) {
                        fail_now("Pair start");
                    }
                }
            }
            return 0;
        case BLE_GAP_EVENT_DISCONNECT:
            s_conn = BLE_HS_CONN_HANDLE_NONE;
            if (s_state != CtsBleState::Success && s_state != CtsBleState::Failed &&
                s_state != CtsBleState::Stopping) {
                fail_now("Dropped");
            } else {
                request_stop();
            }
            return 0;
        case BLE_GAP_EVENT_ENC_CHANGE:
            if (event->enc_change.status != 0) {
                fail_now("Pair fail");
                return 0;
            }
            start_cts_disc(event->enc_change.conn_handle);
            return 0;
        case BLE_GAP_EVENT_REPEAT_PAIRING:
            {
                struct ble_gap_conn_desc desc;
                if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                    ble_store_util_delete_peer(&desc.peer_id_addr);
                }
                return BLE_GAP_REPEAT_PAIRING_RETRY;
            }
        default:
            return 0;
    }
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields;
    std::memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = reinterpret_cast<const uint8_t*>(s_name);
    fields.name_len = static_cast<uint8_t>(std::strlen(s_name));
    fields.name_is_complete = 1;
    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        fail_now("Adv fields");
        return;
    }

    struct ble_gap_adv_params adv;
    std::memset(&adv, 0, sizeof(adv));
    adv.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, nullptr, BLE_HS_FOREVER, &adv, gap_event, nullptr);
    if (rc != 0) {
        fail_now("Adv start");
        return;
    }
    set_state(CtsBleState::Advertising, "Pair phone");
}

static void on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        fail_now("No addr");
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        fail_now("Addr type");
        return;
    }
    ble_svc_gap_device_name_set(s_name);
    advertise();
}

static void on_reset(int reason)
{
    ESP_LOGW(TAG, "host reset %d", reason);
}

static void host_task(void* /*param*/)
{
    ESP_LOGI(TAG, "host task");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t cts_ble_start_iphone(const char* adv_name)
{
    switch (s_state) {
        case CtsBleState::Idle:
        case CtsBleState::Success:
        case CtsBleState::Failed:
            break;
        case CtsBleState::Starting:
        case CtsBleState::Advertising:
        case CtsBleState::Connected:
        case CtsBleState::Reading:
        case CtsBleState::Stopping:
            return ESP_ERR_INVALID_STATE;
    }
    if (s_host_up) {
        return ESP_ERR_INVALID_STATE;
    }

    if (adv_name == nullptr || adv_name[0] == '\0') {
        adv_name = "Mini-FT8";
    }
    std::strncpy(s_name, adv_name, sizeof(s_name) - 1);
    s_name[sizeof(s_name) - 1] = '\0';

    s_conn = BLE_HS_CONN_HANDLE_NONE;
    s_have_result = false;
    s_stop_host = false;
    s_host_up = true;
    s_deadline_us = esp_timer_get_time() + kAdvertiseTimeoutUs;
    set_state(CtsBleState::Starting, "Starting...");

    esp_err_t err = nimble_port_init();
    if (err != ESP_OK) {
        s_host_up = false;
        set_state(CtsBleState::Failed, "Init fail");
        return err;
    }

    ble_hs_cfg.reset_cb = on_reset;
    ble_hs_cfg.sync_cb = on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    ble_store_config_init();

    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

void cts_ble_abort(void)
{
    switch (s_state) {
        case CtsBleState::Idle:
        case CtsBleState::Stopping:
        case CtsBleState::Success:
        case CtsBleState::Failed:
            return;
        case CtsBleState::Starting:
        case CtsBleState::Advertising:
        case CtsBleState::Connected:
        case CtsBleState::Reading:
            fail_now("Abort");
            break;
    }
}

void cts_ble_poll(void)
{
    if (s_state == CtsBleState::Advertising || s_state == CtsBleState::Starting ||
        s_state == CtsBleState::Connected || s_state == CtsBleState::Reading) {
        if (esp_timer_get_time() > s_deadline_us) {
            fail_now("Timeout");
        }
    }

    if (!s_stop_host) {
        return;
    }
    s_stop_host = false;
    set_state(CtsBleState::Stopping, s_menu);
    int rc = nimble_port_stop();
    if (rc == 0) {
        nimble_port_deinit();
    } else {
        ESP_LOGW(TAG, "nimble_port_stop rc=%d", rc);
    }
    s_host_up = false;
    if (s_state == CtsBleState::Stopping) {
        set_state(s_have_result ? CtsBleState::Success : CtsBleState::Failed,
                  s_have_result ? "Time OK" : s_menu);
    }
}

CtsBleState cts_ble_state(void)
{
    return s_state;
}

const char* cts_ble_menu_item(void)
{
    switch (s_state) {
        case CtsBleState::Idle:
            return "Sync iPhone";
        case CtsBleState::Starting:
        case CtsBleState::Advertising:
        case CtsBleState::Connected:
        case CtsBleState::Reading:
        case CtsBleState::Success:
        case CtsBleState::Failed:
        case CtsBleState::Stopping:
            return s_menu;
    }
    return "Sync iPhone";
}

bool cts_ble_take_result(struct timeval* tv)
{
    if (!s_have_result || tv == nullptr) {
        return false;
    }
    *tv = s_tv;
    s_have_result = false;
    if (s_state == CtsBleState::Success) {
        set_state(CtsBleState::Idle, "Sync iPhone");
    }
    return true;
}

bool cts_ble_ui_dirty(void)
{
    bool d = s_ui_dirty;
    s_ui_dirty = false;
    return d;
}

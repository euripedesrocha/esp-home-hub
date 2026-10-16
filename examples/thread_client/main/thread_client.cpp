#include <atomic>
#include <string_view>

#include "esp_console.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_openthread.h"
#include "esp_openthread_lock.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_vfs_eventfd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "openthread/coap.h"
#include "openthread/dataset.h"
#include "openthread/thread.h"

namespace {

constexpr auto *TAG = "thread-client";
constexpr TickType_t TELEMETRY_INTERVAL = pdMS_TO_TICKS(5000);
constexpr char RESOURCE_PATH[] = "device/living-sensor/r";

struct request_state {
    TaskHandle_t task;
    std::atomic_bool changed = false;
};

class scoped_openthread_lock {
public:
    explicit scoped_openthread_lock(TickType_t timeout)
        : acquired(esp_openthread_lock_acquire(timeout)) {}

    ~scoped_openthread_lock() {
        if (acquired) {
            esp_openthread_lock_release();
        }
    }

    scoped_openthread_lock(const scoped_openthread_lock &) = delete;
    scoped_openthread_lock &operator=(const scoped_openthread_lock &) = delete;

    explicit operator bool() const noexcept {
        return acquired;
    }

private:
    bool acquired;
};

void start_console() {
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp32h2>";
    repl_config.max_cmdline_length = 256;
    const esp_console_dev_usb_serial_jtag_config_t hardware_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    esp_console_repl_t *repl = nullptr;
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&hardware_config, &repl_config, &repl));
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}

bool attached() {
    const scoped_openthread_lock locked(portMAX_DELAY);
    if (!locked) {
        return false;
    }
    const otDeviceRole role = otThreadGetDeviceRole(esp_openthread_get_instance());
    return role == OT_DEVICE_ROLE_CHILD || role == OT_DEVICE_ROLE_ROUTER || role == OT_DEVICE_ROLE_LEADER;
}

void on_response(void *context, otMessage *message, const otMessageInfo *, otError result) {
    auto &state = *static_cast<request_state *>(context);
    state.changed.store(result == OT_ERROR_NONE && message != nullptr &&
                        otCoapMessageGetCode(message) == OT_COAP_CODE_CHANGED);
    xTaskNotifyGive(state.task);
}

bool send_reachability(bool reachable) {
    request_state state{
        .task = xTaskGetCurrentTaskHandle(),
        .changed = false,
    };
    otError error = OT_ERROR_NONE;

    {
        const scoped_openthread_lock locked(portMAX_DELAY);
        if (!locked) {
            return false;
        }
        auto *instance = esp_openthread_get_instance();
        otMessageInfo message_info{};
        error = otThreadGetLeaderRloc(instance, &message_info.mPeerAddr);
        message_info.mPeerPort = OT_DEFAULT_COAP_PORT;

        otMessage *request = error == OT_ERROR_NONE ? otCoapNewMessage(instance, nullptr) : nullptr;
        if (request == nullptr && error == OT_ERROR_NONE) {
            error = OT_ERROR_NO_BUFS;
        }
        if (error == OT_ERROR_NONE) {
            error = otCoapMessageInit(request, OT_COAP_TYPE_CONFIRMABLE, OT_COAP_CODE_PUT);
        }
        if (error == OT_ERROR_NONE) {
            error = otCoapMessageAppendUriPathOptions(request, RESOURCE_PATH);
        }
        if (error == OT_ERROR_NONE) {
            error = otCoapMessageSetPayloadMarker(request);
        }

        const std::string_view payload = reachable ? "true" : "false";
        if (error == OT_ERROR_NONE) {
            error = otMessageAppend(request, payload.data(), payload.size());
        }
        if (error == OT_ERROR_NONE) {
            constexpr otCoapTxParameters parameters{
                .mAckTimeout = 1000,
                .mAckRandomFactorNumerator = 1,
                .mAckRandomFactorDenominator = 1,
                .mMaxRetransmit = 2,
            };
            error =
                otCoapSendRequestWithParameters(instance, request, &message_info, &on_response, &state, &parameters);
        }
        if (error != OT_ERROR_NONE && request != nullptr) {
            otMessageFree(request);
        }
    }

    if (error != OT_ERROR_NONE) {
        ESP_LOGW(TAG, "Failed to send CoAP request: %s", otThreadErrorToString(error));
        return false;
    }

    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return state.changed.load();
}

void send_telemetry(void *) {
    while (!attached()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    otError error;
    {
        const scoped_openthread_lock locked(portMAX_DELAY);
        if (!locked) {
            vTaskDelete(nullptr);
            return;
        }
        error = otCoapStart(esp_openthread_get_instance(), OT_DEFAULT_COAP_PORT);
    }
    if (error != OT_ERROR_NONE && error != OT_ERROR_ALREADY) {
        ESP_LOGE(TAG, "Failed to start CoAP: %s", otThreadErrorToString(error));
        vTaskDelete(nullptr);
        return;
    }

    bool reachable = true;
    while (true) {
        if (send_reachability(reachable)) {
            ESP_LOGI(TAG, "Updated living-sensor reachable=%s", reachable ? "true" : "false");
            reachable = !reachable;
        } else {
            ESP_LOGW(TAG, "Reachability update was not acknowledged");
        }
        vTaskDelay(TELEMETRY_INTERVAL);
    }
}

}  // namespace

extern "C" void app_main() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    const esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 3,
    };
    ESP_ERROR_CHECK(esp_vfs_eventfd_register(&eventfd_config));
    start_console();

    static esp_openthread_config_t config{};
    config.netif_config = ESP_NETIF_DEFAULT_OPENTHREAD();
    config.platform_config.radio_config.radio_mode = RADIO_MODE_NATIVE;
    config.platform_config.host_config.host_connection_mode = HOST_CONNECTION_MODE_NONE;
    config.platform_config.port_config.storage_partition_name = "nvs";
    config.platform_config.port_config.netif_queue_size = 10;
    config.platform_config.port_config.task_queue_size = 10;
    ESP_ERROR_CHECK(esp_openthread_start(&config));

    otOperationalDatasetTlvs dataset;
    {
        const scoped_openthread_lock locked(portMAX_DELAY);
        ESP_ERROR_CHECK(locked ? ESP_OK : ESP_ERR_TIMEOUT);
        const otError dataset_error = otDatasetGetActiveTlvs(esp_openthread_get_instance(), &dataset);
        err = esp_openthread_auto_start(dataset_error == OT_ERROR_NONE ? &dataset : nullptr);
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(xTaskCreate(&send_telemetry, "thread_telemetry", 8192, nullptr, 4, nullptr) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}

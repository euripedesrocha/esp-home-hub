#include "hub_thread.hpp"

#include "esp_openthread.h"
#include "esp_openthread_netif_glue.h"
#include "esp_openthread_types.h"
#include "esp_vfs_eventfd.h"
#include "hub_openthread_mutex.hpp"
#include "openthread/dataset.h"
#include "openthread/ip6.h"
#include "openthread/thread.h"

namespace {

esp_err_t stop_thread_protocol() {
    hub_openthread_mutex thread_mutex;
    const std::lock_guard thread_locked(thread_mutex);
    otInstance *instance = esp_openthread_get_instance();
    otError ot_error = otThreadSetEnabled(instance, false);
    if (ot_error == OT_ERROR_NONE) {
        ot_error = otIp6SetEnabled(instance, false);
    }
    return ot_error == OT_ERROR_NONE ? ESP_OK : ESP_FAIL;
}

}  // namespace

hub_thread::hub_thread() {
    const esp_vfs_eventfd_config_t eventfd_config = {
        .max_fds = 4,
    };
    const esp_err_t err = esp_vfs_eventfd_register(&eventfd_config);
    if (err != ESP_OK) {
        throw hub_error(err);
    }
}

hub_thread::~hub_thread() {
    stop();
    esp_vfs_eventfd_unregister();
}

esp_err_t hub_thread::start() {
    const std::lock_guard lifecycle_locked(lifecycle);
    if (started) {
        return ESP_OK;
    }

    static esp_openthread_config_t config{};
    config.netif_config = ESP_NETIF_DEFAULT_OPENTHREAD();
    config.platform_config.radio_config.radio_mode = RADIO_MODE_NATIVE;
    config.platform_config.host_config.host_connection_mode = HOST_CONNECTION_MODE_NONE;
    config.platform_config.port_config.storage_partition_name = "nvs";
    config.platform_config.port_config.netif_queue_size = 10;
    config.platform_config.port_config.task_queue_size = 10;

    esp_err_t err = esp_openthread_start(&config);
    if (err != ESP_OK) {
        return err;
    }
    started = true;

    otOperationalDatasetTlvs dataset;
    {
        hub_openthread_mutex thread_mutex;
        const std::lock_guard thread_locked(thread_mutex);
        const otError dataset_error = otDatasetGetActiveTlvs(esp_openthread_get_instance(), &dataset);
        err = esp_openthread_auto_start(dataset_error == OT_ERROR_NONE ? &dataset : nullptr);
    }
    if (err != ESP_OK) {
        stop_thread_protocol();
        esp_openthread_stop();
        started = false;
    }
    return err;
}

esp_err_t hub_thread::stop() {
    const std::lock_guard lifecycle_locked(lifecycle);
    if (!started) {
        return ESP_OK;
    }

    esp_err_t result = stop_thread_protocol();
    if (result == ESP_OK) {
        result = esp_openthread_stop();
    }
    if (result == ESP_OK) {
        started = false;
    }
    return result;
}

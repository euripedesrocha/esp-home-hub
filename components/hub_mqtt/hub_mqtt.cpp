#include "hub_mqtt.hpp"

#include "esp_log.h"
#include "mosq_broker.h"
#include "sdkconfig.h"

namespace {

constexpr auto *TAG = "hub-mqtt";
char listen_address[] = "0.0.0.0";

}  // namespace

hub_mqtt::hub_mqtt()
    : stopped(xSemaphoreCreateBinary()) {
    if (stopped != nullptr) {
        xSemaphoreGive(stopped);
    }
}

hub_mqtt::~hub_mqtt() {
    stop();
    if (stopped != nullptr) {
        xSemaphoreTake(stopped, portMAX_DELAY);
        vSemaphoreDelete(stopped);
    }
}

esp_err_t hub_mqtt::start() {
    const std::lock_guard serialized(lifecycle);
    const std::lock_guard locked(guard_state);
    return create_task_locked();
}

esp_err_t hub_mqtt::stop() {
    const std::lock_guard serialized(lifecycle);
    TaskHandle_t active_task = nullptr;
    {
        const std::lock_guard locked(guard_state);
        active_task = task;
    }
    if (active_task != nullptr) {
        mosq_broker_stop();
        xSemaphoreTake(stopped, portMAX_DELAY);
    }
    return ESP_OK;
}

std::uint16_t hub_mqtt::port() const noexcept {
    return CONFIG_HUB_MQTT_PORT;
}

void hub_mqtt::broker_task(void *arg) {
    static_cast<hub_mqtt *>(arg)->run_broker();
}

esp_err_t hub_mqtt::create_task_locked() {
    if (task != nullptr) {
        return ESP_OK;
    }
    if (stopped == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(stopped, 0);
    return xTaskCreate(&hub_mqtt::broker_task, "hub_mqtt", CONFIG_HUB_MQTT_TASK_STACK_SIZE, this, tskIDLE_PRIORITY + 1,
                       &task) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

void hub_mqtt::run_broker() {
    mosq_broker_config config{};
    config.host = listen_address;
    config.port = CONFIG_HUB_MQTT_PORT;
    ESP_LOGI(TAG, "Broker listening on %s:%d", config.host, config.port);
    const int result = mosq_broker_run(&config);
    if (result != 0) {
        ESP_LOGE(TAG, "Broker exited with result %d", result);
    }

    {
        const std::lock_guard locked(guard_state);
        task = nullptr;
        if (stopped != nullptr) {
            xSemaphoreGive(stopped);
        }
    }
    vTaskDelete(nullptr);
}

#include "hub_mqtt.hpp"

#include "esp_log.h"
#include "mosq_broker.h"
#include "sdkconfig.h"

namespace {

constexpr auto *TAG = "hub-mqtt";
char listen_address[] = "0.0.0.0";

}  // namespace

hub_mqtt::hub_mqtt(message_callback source, void *arg)
    : stopped(xSemaphoreCreateBinary()), callback(source), callback_arg(arg) {
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

hub_mqtt::callback_router &hub_mqtt::callback_router::shared() noexcept {
    static callback_router router;
    return router;
}

bool hub_mqtt::callback_router::reserve(hub_mqtt &broker) {
    callback_router &router = shared();
    const std::lock_guard locked(router.guard);
    if (router.instance != nullptr) {
        return false;
    }
    router.instance = &broker;
    return true;
}

void hub_mqtt::callback_router::release(hub_mqtt &broker) noexcept {
    callback_router &router = shared();
    const std::lock_guard locked(router.guard);
    if (router.instance == &broker) {
        router.instance = nullptr;
    }
}

void hub_mqtt::callback_router::on_message(char *, char *topic, char *data, int length, int, int) {
    shared().dispatch(topic, data, length);
}

void hub_mqtt::callback_router::dispatch(char *topic, char *data, int length) noexcept {
    const std::lock_guard locked(guard);
    if (instance == nullptr || instance->callback == nullptr || topic == nullptr || length < 0 ||
        (data == nullptr && length != 0)) {
        return;
    }
    instance->callback(instance->callback_arg, topic,
                       std::span<const std::byte>(reinterpret_cast<const std::byte *>(data), length));
}

esp_err_t hub_mqtt::create_task_locked() {
    if (task != nullptr) {
        return ESP_OK;
    }
    if (stopped == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    if (!callback_router::reserve(*this)) {
        return ESP_ERR_INVALID_STATE;
    }
    xSemaphoreTake(stopped, 0);
    if (xTaskCreate(&hub_mqtt::broker_task, "hub_mqtt", CONFIG_HUB_MQTT_TASK_STACK_SIZE, this, tskIDLE_PRIORITY + 1,
                    &task) == pdPASS) {
        return ESP_OK;
    }
    callback_router::release(*this);
    xSemaphoreGive(stopped);
    return ESP_ERR_NO_MEM;
}

void hub_mqtt::run_broker() {
    mosq_broker_config config{};
    config.host = listen_address;
    config.port = CONFIG_HUB_MQTT_PORT;
    config.handle_message_cb = &callback_router::on_message;
    ESP_LOGI(TAG, "Broker listening on %s:%d", config.host, config.port);
    const int result = mosq_broker_run(&config);
    callback_router::release(*this);
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

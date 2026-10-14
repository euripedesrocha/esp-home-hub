#pragma once

#include <cstdint>
#include <mutex>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

class hub_mqtt {
public:
    hub_mqtt();
    ~hub_mqtt();
    hub_mqtt(const hub_mqtt &) = delete ("broker task is owned by this object");
    hub_mqtt &operator=(const hub_mqtt &) = delete ("broker task is owned by this object");

    esp_err_t start();
    esp_err_t stop();
    std::uint16_t port() const noexcept;

private:
    static void broker_task(void *arg);
    esp_err_t create_task_locked();
    void run_broker();

    SemaphoreHandle_t stopped = nullptr;
    TaskHandle_t task = nullptr;
    std::mutex guard_state;
    std::mutex lifecycle;
};

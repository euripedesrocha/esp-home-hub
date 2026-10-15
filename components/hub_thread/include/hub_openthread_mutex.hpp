#pragma once

#include "esp_err.h"
#include "esp_openthread_lock.h"
#include "freertos/FreeRTOS.h"

class hub_openthread_mutex {
public:
    void lock() {
        ESP_ERROR_CHECK(esp_openthread_lock_acquire(portMAX_DELAY) ? ESP_OK : ESP_ERR_TIMEOUT);
    }

    void unlock() noexcept {
        esp_openthread_lock_release();
    }
};

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <span>
#include <string_view>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

class hub_mqtt {
public:
    using message_callback = void (*)(void *, std::string_view, std::span<const std::byte>) noexcept;

    hub_mqtt(message_callback callback, void *arg);
    ~hub_mqtt();
    hub_mqtt(const hub_mqtt &) = delete ("broker task is owned by this object");
    hub_mqtt &operator=(const hub_mqtt &) = delete ("broker task is owned by this object");

    esp_err_t start();
    esp_err_t stop();
    std::uint16_t port() const noexcept;

private:
    class callback_router {
    public:
        static callback_router &shared() noexcept;
        static bool reserve(hub_mqtt &broker);
        static void release(hub_mqtt &broker) noexcept;
        static void on_message(char *client, char *topic, char *data, int length, int qos, int retain);

    private:
        void dispatch(char *topic, char *data, int length) noexcept;

        hub_mqtt *instance = nullptr;
        std::mutex guard;
    };

    static void broker_task(void *arg);
    esp_err_t create_task_locked();
    void run_broker();

    SemaphoreHandle_t stopped = nullptr;
    TaskHandle_t task = nullptr;
    message_callback callback;
    void *callback_arg;
    std::mutex guard_state;
    std::mutex lifecycle;
};

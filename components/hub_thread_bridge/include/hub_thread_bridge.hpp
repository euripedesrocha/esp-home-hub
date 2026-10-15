#pragma once

#include <exception>

#include "esp_err.h"
#include "hub_events.hpp"

class hub_thread_bridge {
public:
    static constexpr unsigned short PORT = 5683;

    class error : public std::exception {
    public:
        explicit error(esp_err_t code) noexcept
            : failure(code) {}
        esp_err_t code() const noexcept {
            return failure;
        }
        const char *what() const noexcept override {
            return esp_err_to_name(failure);
        }

    private:
        esp_err_t failure;
    };

    explicit hub_thread_bridge(hub_events &events);
    ~hub_thread_bridge();
    hub_thread_bridge(const hub_thread_bridge &) = delete ("the OpenThread CoAP endpoint is owned by this object");
    hub_thread_bridge &
    operator=(const hub_thread_bridge &) = delete ("the OpenThread CoAP endpoint is owned by this object");

    esp_err_t serve();
    esp_err_t stop();

private:
    hub_events &events;
    bool serving = false;
};

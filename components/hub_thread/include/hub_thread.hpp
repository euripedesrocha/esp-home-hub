#pragma once

#include <exception>
#include <mutex>

#include "esp_err.h"

class hub_thread {
public:
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

    hub_thread();
    ~hub_thread();
    hub_thread(const hub_thread &) = delete ("OpenThread resources are owned by this object");
    hub_thread &operator=(const hub_thread &) = delete ("OpenThread resources are owned by this object");

    esp_err_t start();
    esp_err_t stop();

private:
    bool started = false;
    std::mutex lifecycle;
};

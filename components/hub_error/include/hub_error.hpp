#pragma once

#include <exception>

#include "esp_err.h"

class hub_error : public std::exception {
public:
    explicit hub_error(esp_err_t code) noexcept
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

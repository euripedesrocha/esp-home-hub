/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <exception>

#include "esp_err.h"

class hub_mdns {
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

    hub_mdns();
    ~hub_mdns();
    hub_mdns(const hub_mdns &) = delete ("mDNS has a single global service instance");
    hub_mdns &operator=(const hub_mdns &) = delete ("mDNS has a single global service instance");

    esp_err_t advertise();
    esp_err_t withdraw();

private:
    bool advertised = false;
};

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <expected>
#include <exception>
#include <string_view>

#include "esp_err.h"
#include "esp_event.h"
#include "hub_events_types.h"

ESP_EVENT_DECLARE_BASE(HUB_EVENTS);

enum class hub_event_id : int32_t {
    reachability,
};

class hub_events {
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

    hub_events();
    ~hub_events();
    hub_events(const hub_events &) = delete ("event loop resources are owned by this object");
    hub_events &operator=(const hub_events &) = delete ("event loop resources are owned by this object");

    std::expected<esp_event_handler_instance_t, esp_err_t> register_handler(hub_event_id event,
                                                                            esp_event_handler_t handler, void *arg);
    esp_err_t unregister_handler(hub_event_id event, esp_event_handler_instance_t instance);
    esp_err_t publish_reachability(std::string_view id, bool reachable) const;

private:
    esp_event_loop_handle_t event_loop = nullptr;
};

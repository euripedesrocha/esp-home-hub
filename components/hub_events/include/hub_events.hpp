/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <expected>
#include <span>

#include "esp_err.h"
#include "esp_event.h"
#include "hub_error.hpp"
#include "hub_topic.hpp"

ESP_EVENT_DECLARE_BASE(HUB_EVENTS);

class hub_events {
public:
    hub_events();
    ~hub_events();
    hub_events(const hub_events &) = delete ("event loop resources are owned by this object");
    hub_events &operator=(const hub_events &) = delete ("event loop resources are owned by this object");

    std::expected<esp_event_handler_instance_t, esp_err_t> subscribe(hub_topic topic, esp_event_handler_t handler,
                                                                     void *arg);
    esp_err_t unsubscribe(hub_topic topic, esp_event_handler_instance_t instance) noexcept;
    esp_err_t publish(hub_topic topic, std::span<const std::byte> event) const noexcept;

private:
    void *event_loop = nullptr;
};

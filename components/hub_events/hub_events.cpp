/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hub_events.hpp"

#include <cstddef>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <utility>

#include "freertos/FreeRTOS.h"

ESP_EVENT_DEFINE_BASE(HUB_EVENTS);

hub_events::hub_events() {
    const esp_event_loop_args_t config = {
        .queue_size = 8,
        .task_name = "hub_events",
        .task_priority = tskIDLE_PRIORITY + 1,
        .task_stack_size = 4096,
        .task_core_id = tskNO_AFFINITY,
    };
    const esp_err_t err = esp_event_loop_create(&config, &event_loop);
    if (err != ESP_OK) {
        throw error(err);
    }
}

hub_events::~hub_events() {
    if (event_loop != nullptr) {
        esp_event_loop_delete(event_loop);
    }
}

std::expected<esp_event_handler_instance_t, esp_err_t>
hub_events::register_handler(hub_event_id event, esp_event_handler_t handler, void *arg) {
    esp_event_handler_instance_t instance = nullptr;
    const esp_err_t err = esp_event_handler_instance_register_with(event_loop, HUB_EVENTS, std::to_underlying(event),
                                                                   handler, arg, &instance);
    if (err != ESP_OK) {
        return std::unexpected(err);
    }
    return instance;
}

esp_err_t hub_events::unregister_handler(hub_event_id event, esp_event_handler_instance_t instance) {
    return esp_event_handler_instance_unregister_with(event_loop, HUB_EVENTS, std::to_underlying(event), instance);
}

esp_err_t hub_events::publish_reachability(std::string_view id, bool reachable) const {
    if (id.empty() || id.size() > std::numeric_limits<std::size_t>::max() - sizeof(hub_reachability_event_t)) {
        return ESP_ERR_INVALID_ARG;
    }

    const std::size_t event_size = sizeof(hub_reachability_event_t) + id.size();
    const std::unique_ptr<std::byte[]> storage(new (std::nothrow) std::byte[event_size]{});
    if (storage == nullptr) {
        return ESP_ERR_NO_MEM;
    }
    auto *event = ::new (storage.get()) hub_reachability_event_t{
        .id_size = id.size(),
        .reachable = reachable,
    };
    std::memcpy(event->id, id.data(), id.size());
    return esp_event_post_to(event_loop, HUB_EVENTS, std::to_underlying(hub_event_id::reachability), event, event_size,
                             0);
}

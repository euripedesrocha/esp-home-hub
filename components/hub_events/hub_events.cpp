/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hub_events.hpp"

#include "esp_event.h"
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
    esp_event_loop_handle_t handle = nullptr;
    const esp_err_t err = esp_event_loop_create(&config, &handle);
    if (err != ESP_OK) {
        throw error(err);
    }
    event_loop = handle;
}

hub_events::~hub_events() {
    if (event_loop != nullptr) {
        esp_event_loop_delete(static_cast<esp_event_loop_handle_t>(event_loop));
    }
}

std::expected<esp_event_handler_instance_t, esp_err_t> hub_events::subscribe(hub_topic topic,
                                                                             esp_event_handler_t handler, void *arg) {
    esp_event_handler_instance_t instance = nullptr;
    const esp_err_t err = esp_event_handler_instance_register_with(static_cast<esp_event_loop_handle_t>(event_loop),
                                                                   HUB_EVENTS, topic.event_id, handler, arg, &instance);
    if (err != ESP_OK) {
        return std::unexpected(err);
    }
    return instance;
}

esp_err_t hub_events::unsubscribe(hub_topic topic, esp_event_handler_instance_t instance) noexcept {
    return esp_event_handler_instance_unregister_with(static_cast<esp_event_loop_handle_t>(event_loop), HUB_EVENTS,
                                                      topic.event_id, instance);
}

esp_err_t hub_events::publish(hub_topic topic, std::span<const std::byte> event) const noexcept {
    return esp_event_post_to(static_cast<esp_event_loop_handle_t>(event_loop), HUB_EVENTS, topic.event_id, event.data(),
                             event.size(), 0);
}

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hub_system.hpp"

#include <algorithm>
#include <utility>

#include "esp_log.h"
#include "nlohmann/json.hpp"

namespace {

using json_value = nlohmann::json;
constexpr auto *TAG = "hub-system";

bool read_text(const json_value &object, std::string_view key, std::string &destination) {
    const auto value = object.find(key);
    if (value == object.end() || !value->is_string()) {
        return false;
    }
    const std::string_view text = value->get_ref<const std::string &>();
    if (text.empty()) {
        return false;
    }
    destination = text;
    return true;
}

}  // namespace

hub_system::read_view::read_view(std::mutex &guard, const hub_system_definition &definition)
    : lock(guard), current(definition) {}

hub_system::read_view::const_iterator hub_system::read_view::begin() const noexcept {
    return current.devices.cbegin();
}

hub_system::read_view::const_iterator hub_system::read_view::end() const noexcept {
    return current.devices.cend();
}

std::size_t hub_system::read_view::size() const noexcept {
    return current.devices.size();
}

const std::string &hub_system::read_view::name() const noexcept {
    return current.name;
}

hub_system::hub_system(hub_events &source)
    : events(source),
      definition{
          .name = "ESP Home Hub",
          .devices =
              {
                  {.id = "kitchen-light",
                   .name = "Kitchen light",
                   .room = "Kitchen",
                   .kind = "light",
                   .reachable = false},
                  {.id = "living-sensor",
                   .name = "Living sensor",
                   .room = "Living room",
                   .kind = "sensor",
                   .reachable = false},
                  {.id = "bedroom-climate",
                   .name = "Bedroom climate",
                   .room = "Bedroom",
                   .kind = "climate",
                   .reachable = false},
              },
      } {
    const auto registration = events.register_handler(hub_event_id::reachability, &hub_system::on_reachability, this);
    if (!registration) {
        throw error(registration.error());
    }
    reachability_handler = *registration;
}

hub_system::~hub_system() {
    if (reachability_handler != nullptr) {
        events.unregister_handler(hub_event_id::reachability, reachability_handler);
    }
}

hub_system::read_view hub_system::read() const {
    return read_view(guard_state, definition);
}

std::string hub_system::json() const {
    json_value document;
    {
        const read_view current = read();
        document = {
            {"name", current.name()},
            {"devices", json_value::array()},
        };
        for (const hub_device &device : current) {
            document["devices"].push_back({
                {"id", device.id},
                {"name", device.name},
                {"room", device.room},
                {"kind", device.kind},
                {"reachable", device.reachable},
            });
        }
    }
    return document.dump();
}

hub_system::replace_result hub_system::replace(std::string_view document) {
    const json_value root = json_value::parse(document.begin(), document.end(), nullptr, false);
    if (root.is_discarded()) {
        return replace_result::invalid_json;
    }
    if (!root.is_object()) {
        return replace_result::invalid_model;
    }

    hub_system_definition replacement;
    if (!read_text(root, "name", replacement.name)) {
        return replace_result::invalid_model;
    }
    const auto devices = root.find("devices");
    if (devices == root.end() || !devices->is_array()) {
        return replace_result::invalid_model;
    }
    replacement.devices.reserve(devices->size());

    for (const json_value &item : *devices) {
        if (!item.is_object()) {
            return replace_result::invalid_model;
        }
        hub_device device;
        const auto reachable = item.find("reachable");
        if (!read_text(item, "id", device.id) || !read_text(item, "name", device.name) ||
            !read_text(item, "room", device.room) || !read_text(item, "kind", device.kind) || reachable == item.end() ||
            !reachable->is_boolean()) {
            return replace_result::invalid_model;
        }
        const bool duplicate = std::ranges::any_of(
            replacement.devices, [&device](const hub_device &entry) { return entry.id == device.id; });
        if (duplicate) {
            return replace_result::invalid_model;
        }
        device.reachable = reachable->get<bool>();
        replacement.devices.push_back(std::move(device));
    }

    const std::lock_guard locked(guard_state);
    definition = std::move(replacement);
    return replace_result::success;
}

hub_system::update_result hub_system::apply_reachability(std::string_view id, bool reachable) {
    const std::lock_guard locked(guard_state);
    const auto device = std::ranges::find(definition.devices, id, &hub_device::id);
    if (device == definition.devices.end()) {
        return update_result::not_found;
    }
    device->reachable = reachable;
    return update_result::success;
}

void hub_system::on_reachability(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base != HUB_EVENTS || event_id != std::to_underlying(hub_event_id::reachability) ||
        event_data == nullptr) {
        return;
    }
    auto *self = static_cast<hub_system *>(arg);
    const auto *event = static_cast<const hub_reachability_event_t *>(event_data);
    if (self->apply_reachability(std::string_view(event->id, event->id_size), event->reachable) ==
        update_result::not_found) {
        ESP_LOGW(TAG, "reachability update ignored for unknown device");
    }
}

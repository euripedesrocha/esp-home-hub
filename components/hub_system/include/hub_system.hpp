/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "hub_error.hpp"
#include "hub_events.hpp"

struct hub_device {
    std::string id;
    std::string name;
    std::string room;
    std::string kind;
    bool reachable = false;
};

struct hub_system_definition {
    std::string name;
    std::vector<hub_device> devices;
};

class hub_system {
public:
    class read_view {
    public:
        using const_iterator = std::vector<hub_device>::const_iterator;

        read_view(const read_view &) = delete;
        read_view &operator=(const read_view &) = delete;
        read_view(read_view &&) noexcept = default;
        read_view &operator=(read_view &&) = delete;

        const_iterator begin() const noexcept;
        const_iterator end() const noexcept;
        std::size_t size() const noexcept;
        const std::string &name() const noexcept;

    private:
        friend class hub_system;

        read_view(std::mutex &guard, const hub_system_definition &current);

        std::unique_lock<std::mutex> lock;
        const hub_system_definition &current;
    };

    enum class replace_result : std::uint8_t {
        success,
        invalid_json,
        invalid_model,
    };

    explicit hub_system(hub_events &events);
    ~hub_system();
    hub_system(const hub_system &) = delete ("event handlers are registered with this object's address");
    hub_system &operator=(const hub_system &) = delete ("event handlers are registered with this object's address");

    read_view read() const;
    std::string json() const;
    replace_result replace(std::string_view document);

private:
    enum class update_result : std::uint8_t {
        success,
        not_found,
    };

    static void on_message(void *arg, esp_event_base_t event_base, std::int32_t event_id, void *event_data);
    update_result apply_reachability(std::string_view id, bool reachable);

    hub_events &events;
    esp_event_handler_instance_t message_handler = nullptr;
    mutable std::mutex guard_state;
    hub_system_definition definition;
};

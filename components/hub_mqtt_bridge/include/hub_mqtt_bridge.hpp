#pragma once

#include <cstddef>
#include <span>
#include <string_view>

#include "hub_events.hpp"

class hub_mqtt_bridge {
public:
    explicit hub_mqtt_bridge(hub_events &source)
        : events(source) {}

    static void on_message(void *arg, std::string_view topic, std::span<const std::byte> payload) noexcept;

private:
    void publish(std::string_view topic, std::span<const std::byte> payload) const noexcept;

    hub_events &events;
};

#pragma once

#include "hub_events.hpp"

class hub_thread_bridge {
public:
    static constexpr unsigned short PORT = 5683;

    explicit hub_thread_bridge(hub_events &events);
    ~hub_thread_bridge();
    hub_thread_bridge(const hub_thread_bridge &) = delete ("the OpenThread CoAP endpoint is owned by this object");
    hub_thread_bridge &
    operator=(const hub_thread_bridge &) = delete ("the OpenThread CoAP endpoint is owned by this object");

    esp_err_t serve();
    esp_err_t stop();

private:
    hub_events &events;
    bool serving = false;
};

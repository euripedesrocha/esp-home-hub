/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>

#include "hub_error.hpp"

class hub_mdns {
public:
    hub_mdns();
    ~hub_mdns();
    hub_mdns(const hub_mdns &) = delete ("mDNS has a single global service instance");
    hub_mdns &operator=(const hub_mdns &) = delete ("mDNS has a single global service instance");

    esp_err_t advertise();
    esp_err_t withdraw();
    esp_err_t advertise_mqtt(std::uint16_t port);
    esp_err_t withdraw_mqtt();

private:
    bool http_advertised = false;
    bool mqtt_advertised = false;
};

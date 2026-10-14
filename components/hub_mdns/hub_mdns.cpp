/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hub_mdns.hpp"

#include "esp_log.h"
#include "mdns.h"

namespace {

constexpr auto *TAG = "hub-mdns";

}  // namespace

hub_mdns::hub_mdns() {
    esp_err_t err = mdns_init();
    const bool initialized = err == ESP_OK;
    if (err == ESP_OK) {
        err = mdns_hostname_set("esp-home-hub");
    }
    if (err == ESP_OK) {
        err = mdns_instance_name_set("ESP Home Hub");
    }
    if (err != ESP_OK) {
        if (initialized) {
            mdns_free();
        }
        throw error(err);
    }
}

hub_mdns::~hub_mdns() {
    mdns_free();
}

esp_err_t hub_mdns::advertise() {
    if (http_advertised) {
        return ESP_OK;
    }

    const esp_err_t err = mdns_service_add(nullptr, "_http", "_tcp", 80, nullptr, 0);
    if (err == ESP_OK) {
        http_advertised = true;
        ESP_LOGI(TAG, "System view: http://esp-home-hub.local/");
    }
    return err;
}

esp_err_t hub_mdns::withdraw() {
    if (!http_advertised) {
        return ESP_OK;
    }

    const esp_err_t err = mdns_service_remove("_http", "_tcp");
    if (err == ESP_OK) {
        http_advertised = false;
    }
    return err;
}

esp_err_t hub_mdns::advertise_mqtt(std::uint16_t port) {
    if (mqtt_advertised) {
        return ESP_OK;
    }

    const esp_err_t err = mdns_service_add(nullptr, "_mqtt", "_tcp", port, nullptr, 0);
    if (err == ESP_OK) {
        mqtt_advertised = true;
        ESP_LOGI(TAG, "MQTT: mqtt://esp-home-hub.local:%u", port);
    }
    return err;
}

esp_err_t hub_mdns::withdraw_mqtt() {
    if (!mqtt_advertised) {
        return ESP_OK;
    }

    const esp_err_t err = mdns_service_remove("_mqtt", "_tcp");
    if (err == ESP_OK) {
        mqtt_advertised = false;
    }
    return err;
}

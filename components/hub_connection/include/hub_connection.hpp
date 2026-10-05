/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <cstdint>
#include <exception>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"

ESP_EVENT_DECLARE_BASE(HUB_EVENT);

enum {
    HUB_EVENT_CONNECTED,
    HUB_EVENT_DISCONNECTED,
};

struct hub_event_connectivity_t {
    bool connected;
};

class hub_connection {
public:
    enum class interface : std::uint8_t {
        ethernet,
        station,
    };

    struct config {
        std::optional<std::string_view> ssid = std::nullopt;
        std::optional<std::string_view> password = std::nullopt;
        interface preferred = interface::ethernet;
        int sta_max_attempts = 3;
    };

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

    explicit hub_connection(const config &options);
    ~hub_connection();
    hub_connection(const hub_connection &) = delete ("event handlers are registered with this object's address");
    hub_connection &
    operator=(const hub_connection &) = delete ("event handlers are registered with this object's address");

    esp_err_t start();
    esp_err_t start(interface iface);
    esp_err_t stop();
    esp_err_t stop(interface iface);
    esp_err_t set_preferred(interface preferred);
    esp_netif_t *netif(interface iface) const;
    bool connected() const;

private:
    int route_prio(interface iface) const;
    esp_err_t reselect_default();
    void report_connectivity(bool previous_connection_status);
    void unregister_handlers() noexcept;
    esp_err_t start_ethernet();
    esp_err_t start_station();
    esp_err_t stop_ethernet();
    esp_err_t stop_station();

    static void on_eth_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
    static void on_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
    static void on_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

    esp_event_handler_instance_t eth_handler = nullptr;
    esp_event_handler_instance_t wifi_handler = nullptr;
    esp_event_handler_instance_t ip_handler = nullptr;

    struct interface_state {
        esp_netif_t *netif = nullptr;
        bool has_ip = false;
    };

    struct ethernet_state {
        interface_state interface;
        void *handle = nullptr;
        void *glue = nullptr;
    };

    struct station_state {
        interface_state interface;
        bool started = false;
        int retries = 0;
    };

    std::optional<std::string> ssid;
    std::optional<std::string> password;
    interface preferred = interface::ethernet;
    int sta_max_attempts = 0;
    ethernet_state ethernet;
    station_state wifi_sta;
    mutable std::recursive_mutex guard_state;
    std::mutex lifecycle;
};

/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hub_connection.hpp"

#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_phy_yt8531.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"

namespace {

constexpr auto *TAG = "hub-link";
constexpr int PREFERRED_PRIO = 100;
constexpr int STANDBY_PRIO = 10;

}  // namespace

ESP_EVENT_DEFINE_BASE(HUB_EVENT);

esp_netif_t *hub_connection::netif(interface iface) const {
    const std::lock_guard locked(guard_state);
    switch (iface) {
    case interface::ethernet:
        return ethernet.interface.netif;
    case interface::station:
        return wifi_sta.interface.netif;
    }
    return nullptr;
}

int hub_connection::route_prio(interface iface) const {
    return iface == preferred ? PREFERRED_PRIO : STANDBY_PRIO;
}

bool hub_connection::connected() const {
    const std::lock_guard locked(guard_state);
    return ethernet.interface.has_ip || wifi_sta.interface.has_ip;
}

esp_err_t hub_connection::reselect_default() {
    if (ethernet.interface.netif != nullptr) {
        esp_netif_set_route_prio(ethernet.interface.netif, route_prio(interface::ethernet));
    }
    if (wifi_sta.interface.netif != nullptr) {
        esp_netif_set_route_prio(wifi_sta.interface.netif, route_prio(interface::station));
    }
    return esp_netif_set_default_netif(nullptr);
}

void hub_connection::report_connectivity(bool previous_connection_status) {
    if (previous_connection_status == connected()) {
        return;
    }
    if (!connected()) {
        ESP_LOGW(TAG, "Hub disconnected");
    }
    const bool is_connected = connected();
    const hub_event_connectivity_t event = {.connected = is_connected};
    const int32_t id = is_connected ? HUB_EVENT_CONNECTED : HUB_EVENT_DISCONNECTED;
    if (esp_event_post(HUB_EVENT, id, &event, sizeof(event), 0) != ESP_OK) {
        ESP_LOGW(TAG, "connectivity event not posted");
    }
}

esp_err_t hub_connection::set_preferred(interface iface) {
    const std::lock_guard locked(guard_state);
    preferred = iface;
    return reselect_default();
}

esp_err_t hub_connection::start_ethernet() {
    esp_eth_handle_t handle = nullptr;
    esp_err_t err = ESP_OK;
    {
        const std::lock_guard locked(guard_state);
        if (ethernet.handle != nullptr) {
            ESP_LOGW(TAG, "Already started");
            return ESP_ERR_INVALID_STATE;
        }
        eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
        eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
        phy_config.reset_gpio_num = 7;

        eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
        esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
        if (mac == nullptr) {
            return ESP_FAIL;
        }
        esp_eth_phy_t *phy = esp_eth_phy_new_yt8531(&phy_config);
        if (phy == nullptr) {
            mac->del(mac);
            return ESP_FAIL;
        }

        esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
        err = esp_eth_driver_install(&eth_config, reinterpret_cast<esp_eth_handle_t *>(&ethernet.handle));
        if (err != ESP_OK) {
            mac->del(mac);
            phy->del(phy);
            return err;
        }
        handle = static_cast<esp_eth_handle_t>(ethernet.handle);
        yt8531_rgmii_clk_delay_config_t clk_delay = {
            .rxc_dly_en = true,
            .rx_delay_sel = 0,
            .tx_delay_sel = 15,
            .tx_delay_sel_fe = 15,
        };
        err =
            esp_eth_ioctl(ethernet.handle, static_cast<esp_eth_io_cmd_t>(YT8531_ETH_CMD_S_RGMII_CLK_DELAY), &clk_delay);
        if (err == ESP_OK) {
            esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_ETH();
            base.if_desc = "hub_eth";
            base.route_prio = route_prio(interface::ethernet);
            esp_netif_config_t netif_config = {
                .base = &base,
                .driver = nullptr,
                .stack = ESP_NETIF_NETSTACK_DEFAULT_ETH,
            };
            ethernet.interface.netif = esp_netif_new(&netif_config);
            if (ethernet.interface.netif == nullptr) {
                err = ESP_ERR_NO_MEM;
            }
        }
        if (err == ESP_OK) {
            esp_eth_netif_glue_handle_t glue = esp_eth_new_netif_glue(ethernet.handle);
            if (glue == nullptr) {
                err = ESP_FAIL;
            } else {
                ethernet.glue = glue;
                err = esp_netif_attach(ethernet.interface.netif, glue);
            }
        }
    }
    if (err != ESP_OK) {
        if (handle != nullptr) {
            stop_ethernet();
        }
        return err;
    }
    err = esp_eth_start(handle);
    if (err != ESP_OK) {
        stop_ethernet();
    }
    return err;
}

esp_err_t hub_connection::stop_ethernet() {
    esp_netif_t *netif = nullptr;
    esp_eth_handle_t handle = nullptr;
    esp_eth_netif_glue_handle_t glue = nullptr;
    {
        const std::lock_guard locked(guard_state);
        if (ethernet.handle == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        const bool previous_connection_status = connected();
        ethernet.interface.has_ip = false;
        netif = ethernet.interface.netif;
        handle = static_cast<esp_eth_handle_t>(ethernet.handle);
        glue = static_cast<esp_eth_netif_glue_handle_t>(ethernet.glue);
        ethernet.interface.netif = nullptr;
        ethernet.handle = nullptr;
        ethernet.glue = nullptr;
        if (reselect_default() != ESP_OK) {
            ESP_LOGW(TAG, "reselect default failed");
        }
        report_connectivity(previous_connection_status);
    }

    esp_err_t err = esp_eth_stop(handle);
    if (err == ESP_ERR_INVALID_STATE) {
        err = ESP_OK;
    }
    if (glue != nullptr) {
        esp_err_t glue_err = esp_eth_del_netif_glue(glue);
        if (err == ESP_OK) {
            err = glue_err;
        }
    }
    if (netif != nullptr) {
        esp_netif_destroy(netif);
    }

    esp_eth_mac_t *mac = nullptr;
    esp_eth_phy_t *phy = nullptr;
    esp_eth_get_mac_instance(handle, &mac);
    esp_eth_get_phy_instance(handle, &phy);
    esp_err_t uninstall_err = esp_eth_driver_uninstall(handle);
    if (err == ESP_OK) {
        err = uninstall_err;
    }
    if (uninstall_err == ESP_OK) {
        if (mac != nullptr) {
            mac->del(mac);
        }
        if (phy != nullptr) {
            phy->del(phy);
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ethernet stop failed: %s", esp_err_to_name(err));
    }
    return err;
}

esp_err_t hub_connection::start_station() {
    esp_err_t err = ESP_OK;
    {
        const std::lock_guard locked(guard_state);
        if (wifi_sta.interface.netif != nullptr) {
            ESP_LOGW(TAG, "Already started");
            return ESP_ERR_INVALID_STATE;
        }
        esp_netif_inherent_config_t base = ESP_NETIF_INHERENT_DEFAULT_WIFI_STA();
        base.if_desc = "hub_sta";
        base.route_prio = route_prio(interface::station);
        wifi_sta.interface.netif = esp_netif_create_wifi(WIFI_IF_STA, &base);
        if (wifi_sta.interface.netif == nullptr) {
            return ESP_ERR_NO_MEM;
        }
        err = esp_wifi_set_default_wifi_sta_handlers();
        if (err == ESP_OK) {
            wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
            err = esp_wifi_init(&init_config);
        }
        if (err == ESP_OK) {
            err = esp_wifi_set_storage(WIFI_STORAGE_FLASH);
        }
        if (err == ESP_OK) {
            err = esp_wifi_set_mode(WIFI_MODE_STA);
        }
        if (err == ESP_OK) {
            wifi_config_t wifi_config = {};
            const std::string pass = password.value_or("");
            ssid->copy(reinterpret_cast<char *>(wifi_config.sta.ssid), sizeof(wifi_config.sta.ssid) - 1);
            pass.copy(reinterpret_cast<char *>(wifi_config.sta.password), sizeof(wifi_config.sta.password) - 1);
            wifi_config.sta.threshold.authmode = pass.empty() ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
            err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
        }
    }
    if (err != ESP_OK) {
        stop_station();
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK) {
        stop_station();
        return err;
    }
    {
        const std::lock_guard locked(guard_state);
        wifi_sta.started = true;
        wifi_sta.retries = 0;
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        stop_station();
    }
    return err;
}

esp_err_t hub_connection::stop_station() {
    esp_netif_t *netif = nullptr;
    {
        const std::lock_guard locked(guard_state);
        if (wifi_sta.interface.netif == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }
        wifi_sta.started = false;
        const bool previous_connection_status = connected();
        wifi_sta.interface.has_ip = false;
        wifi_sta.retries = 0;
        netif = wifi_sta.interface.netif;
        wifi_sta.interface.netif = nullptr;
        if (reselect_default() != ESP_OK) {
            ESP_LOGW(TAG, "reselect default failed");
        }
        report_connectivity(previous_connection_status);
    }

    esp_err_t err = esp_wifi_stop();
    if (err == ESP_ERR_WIFI_NOT_STARTED || err == ESP_ERR_WIFI_NOT_INIT) {
        err = ESP_OK;
    }
    esp_err_t deinit_err = esp_wifi_deinit();
    if (deinit_err == ESP_ERR_WIFI_NOT_INIT) {
        deinit_err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = deinit_err;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "wifi stop failed: %s", esp_err_to_name(err));
    }
    esp_err_t clear_err = esp_wifi_clear_default_wifi_driver_and_handlers(netif);
    if (clear_err != ESP_OK) {
        ESP_LOGE(TAG, "clear wifi handlers failed: %s", esp_err_to_name(clear_err));
    }
    esp_netif_destroy(netif);
    return err != ESP_OK ? err : clear_err;
}

void hub_connection::on_eth_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)event_base;
    (void)event_data;
    auto *self = static_cast<hub_connection *>(arg);
    if (event_id != ETHERNET_EVENT_DISCONNECTED) {
        return;
    }
    const std::lock_guard locked(self->guard_state);
    if (self->ethernet.interface.netif != nullptr) {
        const bool previous_connection_status = self->connected();
        self->ethernet.interface.has_ip = false;
        ESP_LOGI(TAG, "Ethernet link down");
        if (self->reselect_default() != ESP_OK) {
            ESP_LOGW(TAG, "reselect default failed");
        }
        self->report_connectivity(previous_connection_status);
    }
}

void hub_connection::on_wifi_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)event_base;
    auto *self = static_cast<hub_connection *>(arg);
    if (event_id != WIFI_EVENT_STA_DISCONNECTED) {
        return;
    }
    bool reconnect = false;
    {
        const std::lock_guard locked(self->guard_state);
        if (!self->wifi_sta.started) {
            return;
        }
        const bool previous_connection_status = self->connected();
        self->wifi_sta.interface.has_ip = false;
        if (self->reselect_default() != ESP_OK) {
            ESP_LOGW(TAG, "reselect default failed");
        }
        self->report_connectivity(previous_connection_status);
        self->wifi_sta.retries++;
        auto *disconnected = static_cast<wifi_event_sta_disconnected_t *>(event_data);
        if (self->wifi_sta.retries >= self->sta_max_attempts) {
            ESP_LOGW(TAG, "Wi-Fi join failed, reason %d, Ethernet stays up", disconnected->reason);
            return;
        }
        reconnect = true;
    }
    if (reconnect) {
        esp_wifi_connect();
    }
}

void hub_connection::on_ip_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)event_base;
    auto *self = static_cast<hub_connection *>(arg);
    const bool got_ip = event_id == IP_EVENT_ETH_GOT_IP || event_id == IP_EVENT_STA_GOT_IP;
    const bool lost_ip = event_id == IP_EVENT_ETH_LOST_IP || event_id == IP_EVENT_STA_LOST_IP;
    if (!got_ip && !lost_ip) {
        return;
    }
    if (event_data == nullptr) {
        return;
    }
    const std::lock_guard locked(self->guard_state);
    const bool previous_connection_status = self->connected();
    auto *event = static_cast<ip_event_got_ip_t *>(event_data);
    const bool ethernet_event = event_id == IP_EVENT_ETH_GOT_IP || event_id == IP_EVENT_ETH_LOST_IP;
    esp_netif_t *active = ethernet_event ? self->ethernet.interface.netif : self->wifi_sta.interface.netif;
    if (event->esp_netif != active) {
        return;
    }
    if (ethernet_event) {
        self->ethernet.interface.has_ip = got_ip;
        if (got_ip) {
            ESP_LOGI(TAG, "Ethernet address: " IPSTR, IP2STR(&event->ip_info.ip));
        }
    } else if (!self->wifi_sta.started && got_ip) {
        return;
    } else {
        self->wifi_sta.interface.has_ip = got_ip;
        if (got_ip) {
            self->wifi_sta.retries = 0;
            ESP_LOGI(TAG, "Wi-Fi address: " IPSTR, IP2STR(&event->ip_info.ip));
        }
    }
    if (self->reselect_default() != ESP_OK) {
        ESP_LOGW(TAG, "reselect default failed");
    }
    self->report_connectivity(previous_connection_status);
}

void hub_connection::unregister_handlers() noexcept {
    if (eth_handler != nullptr) {
        esp_event_handler_instance_unregister(ETH_EVENT, ESP_EVENT_ANY_ID, eth_handler);
        eth_handler = nullptr;
    }
    if (wifi_handler != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, wifi_handler);
        wifi_handler = nullptr;
    }
    if (ip_handler != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, ESP_EVENT_ANY_ID, ip_handler);
        ip_handler = nullptr;
    }
}

hub_connection::hub_connection(const config &options)
    : ssid(options.ssid.transform([](std::string_view value) { return std::string(value); })),
      password(options.password.transform([](std::string_view value) { return std::string(value); })),
      preferred(options.preferred),
      sta_max_attempts(options.sta_max_attempts) {
    if (esp_err_t err = esp_event_handler_instance_register(ETH_EVENT, ESP_EVENT_ANY_ID, &hub_connection::on_eth_event,
                                                            this, &eth_handler);
        err != ESP_OK) {
        throw hub_error(err);
    }
    if (esp_err_t err = esp_event_handler_instance_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED,
                                                            &hub_connection::on_wifi_event, this, &wifi_handler);
        err != ESP_OK) {
        unregister_handlers();
        throw hub_error(err);
    }
    if (esp_err_t err = esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &hub_connection::on_ip_event,
                                                            this, &ip_handler);
        err != ESP_OK) {
        unregister_handlers();
        throw hub_error(err);
    }
}

hub_connection::~hub_connection() {
    bool up = false;
    {
        const std::lock_guard locked(guard_state);
        up = ethernet.handle != nullptr || wifi_sta.interface.netif != nullptr;
    }
    if (up) {
        stop();
    }
    unregister_handlers();
}

esp_err_t hub_connection::start(interface iface) {
    const std::lock_guard locked(lifecycle);
    switch (iface) {
    case interface::ethernet:
        return start_ethernet();
    case interface::station:
        if (!ssid.has_value()) {
            return ESP_ERR_INVALID_ARG;
        }
        return start_station();
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t hub_connection::stop(interface iface) {
    const std::lock_guard locked(lifecycle);
    switch (iface) {
    case interface::ethernet:
        return stop_ethernet();
    case interface::station:
        return stop_station();
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t hub_connection::stop() {
    const std::lock_guard lifecycle_locked(lifecycle);
    bool ethernet_up = false;
    bool station_up = false;
    {
        const std::lock_guard locked(guard_state);
        ethernet_up = ethernet.handle != nullptr;
        station_up = wifi_sta.interface.netif != nullptr;
        if (!ethernet_up && !station_up) {
            ESP_LOGW(TAG, "Already stopped");
            return ESP_ERR_INVALID_STATE;
        }
    }
    esp_err_t err = ESP_OK;
    if (ethernet_up) {
        esp_err_t eth_err = stop_ethernet();
        if (eth_err != ESP_OK) {
            ESP_LOGW(TAG, "ethernet stop failed: %s", esp_err_to_name(eth_err));
            err = eth_err;
        }
    }
    if (station_up) {
        esp_err_t sta_err = stop_station();
        if (sta_err != ESP_OK) {
            ESP_LOGW(TAG, "wifi stop failed: %s", esp_err_to_name(sta_err));
            if (err == ESP_OK) {
                err = sta_err;
            }
        }
    }
    return err;
}

esp_err_t hub_connection::start() {
    const std::lock_guard lifecycle_locked(lifecycle);
    bool ethernet_down = false;
    bool station_down = false;
    {
        const std::lock_guard locked(guard_state);
        ethernet_down = ethernet.handle == nullptr;
        station_down = ssid.has_value() && wifi_sta.interface.netif == nullptr;
        if (!ethernet_down && !station_down) {
            ESP_LOGW(TAG, "Already started");
            return ESP_ERR_INVALID_STATE;
        }
    }
    if (ethernet_down) {
        ESP_RETURN_ON_ERROR(start_ethernet(), TAG, "ethernet start failed");
    }
    if (station_down) {
        ESP_RETURN_ON_ERROR(start_station(), TAG, "wifi start failed");
    }
    return ESP_OK;
}

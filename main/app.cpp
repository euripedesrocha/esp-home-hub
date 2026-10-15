/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hub_connection.hpp"

#include <new>
#include <system_error>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "hub_events.hpp"
#include "hub_mdns.hpp"
#include "hub_mqtt.hpp"
#include "hub_mqtt_bridge.hpp"
#include "hub_system.hpp"
#include "hub_thread.hpp"
#include "hub_thread_bridge.hpp"
#include "hub_web.hpp"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace {

constexpr auto *TAG = "app";

struct network_services {
    hub_mqtt &mqtt;
    hub_mdns &mdns;
};

void on_hub_connectivity(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base != HUB_EVENT || event_data == nullptr) {
        return;
    }
    auto &services = *static_cast<network_services *>(arg);
    const auto *event = static_cast<const hub_event_connectivity_t *>(event_data);
    if (event_id == HUB_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "HUB_EVENT_CONNECTED");
        const esp_err_t start = services.mqtt.start();
        if (start != ESP_OK) {
            ESP_LOGE(TAG, "MQTT broker start failed: %s", esp_err_to_name(start));
        } else {
            const esp_err_t advertise = services.mdns.advertise_mqtt(services.mqtt.port());
            if (advertise != ESP_OK) {
                ESP_LOGE(TAG, "MQTT mDNS advertisement failed: %s", esp_err_to_name(advertise));
                services.mqtt.stop();
            }
        }
    } else if (event_id == HUB_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "HUB_EVENT_DISCONNECTED");
        const esp_err_t withdraw = services.mdns.withdraw_mqtt();
        if (withdraw != ESP_OK) {
            ESP_LOGE(TAG, "MQTT mDNS withdrawal failed: %s", esp_err_to_name(withdraw));
        }
        const esp_err_t stop = services.mqtt.stop();
        if (stop != ESP_OK) {
            ESP_LOGE(TAG, "MQTT broker stop failed: %s", esp_err_to_name(stop));
        }
    }
    ESP_LOGI(TAG, "link connected: %s", event->connected ? "yes" : "no");
}

}  // namespace

extern "C" void app_main(void) {
    ESP_ERROR_CHECK(esp_netif_init());

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    hub_connection::config config = {
        .preferred = hub_connection::interface::ethernet,
    };
    if (!std::string_view(CONFIG_HUB_WIFI_SSID).empty()) {
        config.ssid = CONFIG_HUB_WIFI_SSID;
        config.password = CONFIG_HUB_WIFI_PASSWORD;
    }
    try {
        hub_events events;
        hub_system system(events);
        hub_web web(system);
        hub_mdns mdns;
        hub_mqtt_bridge mqtt_bridge(events);
        hub_mqtt mqtt(&hub_mqtt_bridge::on_message, &mqtt_bridge);
        hub_connection link(config);
        network_services services{.mqtt = mqtt, .mdns = mdns};
        ESP_ERROR_CHECK(esp_event_handler_register(HUB_EVENT, ESP_EVENT_ANY_ID, &on_hub_connectivity, &services));
        ESP_ERROR_CHECK(link.start());
        hub_thread thread;
        ESP_ERROR_CHECK(thread.start());
        hub_thread_bridge thread_bridge(events);
        ESP_ERROR_CHECK(thread_bridge.serve());
        ESP_ERROR_CHECK(web.serve());
        ESP_ERROR_CHECK(mdns.advertise());
        vTaskSuspend(nullptr);
    } catch (const hub_connection::error &failure) {
        ESP_ERROR_CHECK(failure.code());
    } catch (const hub_mdns::error &failure) {
        ESP_ERROR_CHECK(failure.code());
    } catch (const hub_system::error &failure) {
        ESP_ERROR_CHECK(failure.code());
    } catch (const hub_events::error &failure) {
        ESP_ERROR_CHECK(failure.code());
    } catch (const hub_thread::error &failure) {
        ESP_ERROR_CHECK(failure.code());
    } catch (const hub_thread_bridge::error &failure) {
        ESP_ERROR_CHECK(failure.code());
    } catch (const std::bad_alloc &) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    } catch (const std::system_error &) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
}

#include "hub_mqtt_bridge.hpp"

#include <cstring>
#include <memory>
#include <new>

#include "esp_log.h"

namespace {

constexpr auto *TAG = "hub-mqtt-bridge";
constexpr std::size_t MAX_MESSAGE_SIZE = 256;
constexpr hub_topic message_topic{"hub.message"};

struct message_event {
    std::size_t topic_size;
    std::size_t payload_size;
    std::byte data[];
};

}  // namespace

void hub_mqtt_bridge::on_message(void *arg, std::string_view topic, std::span<const std::byte> payload) noexcept {
    if (arg == nullptr) {
        return;
    }
    static_cast<hub_mqtt_bridge *>(arg)->publish(topic, payload);
}

void hub_mqtt_bridge::publish(std::string_view topic, std::span<const std::byte> payload) const noexcept {
    if (topic.empty() || topic.size() > MAX_MESSAGE_SIZE || payload.size() > MAX_MESSAGE_SIZE - topic.size()) {
        ESP_LOGW(TAG, "Ignoring MQTT message with invalid size");
        return;
    }

    const std::size_t event_size = sizeof(message_event) + topic.size() + payload.size();
    try {
        const std::unique_ptr<std::byte[]> storage(new (std::nothrow) std::byte[event_size]{});
        if (storage == nullptr) {
            ESP_LOGE(TAG, "MQTT message event allocation failed");
            return;
        }
        auto *event = ::new (storage.get()) message_event{
            .topic_size = topic.size(),
            .payload_size = payload.size(),
        };
        std::memcpy(event->data, topic.data(), topic.size());
        if (!payload.empty()) {
            std::memcpy(event->data + topic.size(), payload.data(), payload.size());
        }
        const esp_err_t result = events.publish(message_topic, std::span<const std::byte>(storage.get(), event_size));
        if (result != ESP_OK) {
            ESP_LOGW(TAG, "MQTT message event not published: %s", esp_err_to_name(result));
        }
    } catch (const std::bad_alloc &) {
        ESP_LOGE(TAG, "MQTT message allocation failed");
    }
}

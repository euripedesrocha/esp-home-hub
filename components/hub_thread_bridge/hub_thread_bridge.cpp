#include "hub_thread_bridge.hpp"

#include <array>
#include <cstring>
#include <expected>
#include <memory>
#include <mutex>
#include <new>
#include <string_view>

#include "esp_log.h"
#include "esp_openthread.h"
#include "hub_openthread_mutex.hpp"
#include "openthread/coap.h"
#include "openthread/message.h"

namespace {

constexpr auto *TAG = "thread-bridge";
constexpr std::size_t MAX_MESSAGE_SIZE = 256;
constexpr hub_topic message_topic{"hub.message"};
constexpr std::string_view compact_device_prefix = "device/";
constexpr std::string_view compact_reachability_suffix = "/r";
constexpr std::string_view device_prefix = "hub/device/";
constexpr std::string_view reachability_suffix = "/reachability";

struct message_event {
    std::size_t topic_size;
    std::size_t payload_size;
    std::byte data[];
};

struct topic_mapping {
    std::array<std::string_view, 3> parts;

    std::size_t size() const noexcept {
        std::size_t result = 0;
        for (const std::string_view part : parts) {
            result += part.size();
        }
        return result;
    }

    void copy_to(std::byte *output) const noexcept {
        for (const std::string_view part : parts) {
            if (part.empty()) {
                continue;
            }
            std::memcpy(output, part.data(), part.size());
            output += part.size();
        }
    }
};

topic_mapping map_topic(std::string_view topic) {
    std::string_view path = topic;
    if (!path.starts_with(compact_device_prefix) || !path.ends_with(compact_reachability_suffix)) {
        return {.parts = {topic}};
    }

    path.remove_prefix(compact_device_prefix.size());
    path.remove_suffix(compact_reachability_suffix.size());
    if (path.empty() || path.find('/') != std::string_view::npos) {
        return {.parts = {topic}};
    }
    return {.parts = {device_prefix, path, reachability_suffix}};
}

std::expected<std::size_t, otCoapCode> read_topic(otMessage *request, std::span<std::byte> output) {
    otCoapOptionIterator iterator;
    if (otCoapMessageGetCode(request) != OT_COAP_CODE_PUT ||
        otCoapOptionIteratorInit(&iterator, request) != OT_ERROR_NONE) {
        return std::unexpected(OT_COAP_CODE_BAD_REQUEST);
    }

    std::size_t size = 0;
    const auto *option = otCoapOptionIteratorGetFirstOptionMatching(&iterator, OT_COAP_OPTION_URI_PATH);
    while (option != nullptr) {
        if (option->mLength > output.size() - size) {
            return std::unexpected(OT_COAP_CODE_REQUEST_TOO_LARGE);
        }
        if (otCoapOptionIteratorGetOptionValue(&iterator, output.data() + size) != OT_ERROR_NONE) {
            return std::unexpected(OT_COAP_CODE_BAD_REQUEST);
        }
        size += option->mLength;
        option = otCoapOptionIteratorGetNextOptionMatching(&iterator, OT_COAP_OPTION_URI_PATH);
        if (option != nullptr) {
            if (size == output.size()) {
                return std::unexpected(OT_COAP_CODE_REQUEST_TOO_LARGE);
            }
            output[size++] = std::byte{'/'};
        }
    }
    return size;
}

std::expected<void, otCoapCode> publish_message(hub_events &events, const topic_mapping &topic, otMessage *request,
                                                uint16_t payload_offset, std::size_t payload_size) {
    const std::size_t topic_size = topic.size();
    const std::size_t event_size = sizeof(message_event) + topic_size + payload_size;
    const std::unique_ptr<std::byte[]> storage(new (std::nothrow) std::byte[event_size]);
    if (storage == nullptr) {
        ESP_LOGE(TAG, "Thread message allocation failed");
        return std::unexpected(OT_COAP_CODE_SERVICE_UNAVAILABLE);
    }
    auto *event = ::new (storage.get()) message_event{
        .topic_size = topic_size,
        .payload_size = payload_size,
    };
    topic.copy_to(event->data);
    if (otMessageRead(request, payload_offset, event->data + topic_size, payload_size) != payload_size) {
        return std::unexpected(OT_COAP_CODE_BAD_REQUEST);
    }
    const esp_err_t result = events.publish(message_topic, std::span<const std::byte>(storage.get(), event_size));
    if (result != ESP_OK) {
        ESP_LOGW(TAG, "Thread message event not published: %s", esp_err_to_name(result));
        return std::unexpected(OT_COAP_CODE_SERVICE_UNAVAILABLE);
    }
    return {};
}

std::expected<void, otCoapCode> publish_request(hub_events &events, otMessage *request, std::string_view topic) {
    const uint16_t payload_offset = otMessageGetOffset(request);
    const uint16_t message_size = otMessageGetLength(request);
    if (topic.empty() || payload_offset >= message_size) {
        return std::unexpected(OT_COAP_CODE_BAD_REQUEST);
    }

    const std::size_t payload_size = message_size - payload_offset;
    if (payload_size > MAX_MESSAGE_SIZE - topic.size()) {
        return std::unexpected(OT_COAP_CODE_REQUEST_TOO_LARGE);
    }
    return publish_message(events, map_topic(topic), request, payload_offset, payload_size);
}

void send_response(otMessage *request, const otMessageInfo *message_info, otCoapCode code) {
    otInstance *instance = esp_openthread_get_instance();
    if (auto *response = otCoapNewMessage(instance, nullptr); response != nullptr) {
        const auto response_type = otCoapMessageGetType(request) == OT_COAP_TYPE_CONFIRMABLE
                                       ? OT_COAP_TYPE_ACKNOWLEDGMENT
                                       : OT_COAP_TYPE_NON_CONFIRMABLE;
        if (auto result = otCoapMessageInitResponse(response, request, response_type, code); result == OT_ERROR_NONE) {
            result = otCoapSendResponse(instance, response, message_info);
            if (result == OT_ERROR_NONE) {
                return;
            }
        }
        otMessageFree(response);
    }
}

void handle_request(void *context, otMessage *request, const otMessageInfo *message_info) {
    auto &events = *static_cast<hub_events *>(context);
    std::array<std::byte, MAX_MESSAGE_SIZE> data;
    const auto result = read_topic(request, data).and_then([&](std::size_t topic_size) {
        const std::string_view topic(reinterpret_cast<const char *>(data.data()), topic_size);
        return publish_request(events, request, topic);
    });
    send_response(request, message_info, result ? OT_COAP_CODE_CHANGED : result.error());
}

}  // namespace

hub_thread_bridge::hub_thread_bridge(hub_events &source)
    : events(source) {}

hub_thread_bridge::~hub_thread_bridge() {
    stop();
}

esp_err_t hub_thread_bridge::serve() {
    if (serving) {
        return ESP_OK;
    }
    otError result;
    hub_openthread_mutex thread_mutex;
    const std::lock_guard thread_locked(thread_mutex);
    otInstance *instance = esp_openthread_get_instance();
    if (result = otCoapStart(instance, PORT); result == OT_ERROR_NONE || result == OT_ERROR_ALREADY) {
        otCoapSetDefaultHandler(instance, &handle_request, &events);
        serving = true;
        ESP_LOGI(TAG, "Listening for Thread CoAP PUT requests on UDP port %u", PORT);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to start OpenThread CoAP: %s", otThreadErrorToString(result));
    return ESP_FAIL;
}

esp_err_t hub_thread_bridge::stop() {
    if (!serving) {
        return ESP_OK;
    }
    hub_openthread_mutex thread_mutex;
    const std::lock_guard thread_locked(thread_mutex);
    otInstance *instance = esp_openthread_get_instance();
    otCoapSetDefaultHandler(instance, nullptr, nullptr);
    const otError result = otCoapStop(instance);
    if (result != OT_ERROR_NONE) {
        return ESP_FAIL;
    }
    serving = false;
    return ESP_OK;
}

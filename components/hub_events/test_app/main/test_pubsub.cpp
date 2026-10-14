#include <array>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "hub_events.hpp"

namespace {

struct observed_update {
    SemaphoreHandle_t received;
    std::array<char, 32> id{};
    std::size_t id_size = 0;
    bool reachable = false;
    unsigned count = 0;
};

struct reachability_event {
    std::size_t id_size;
    bool reachable;
    char id[];
};

constexpr hub_topic reachability_topic{"hub.device.reachability"};
constexpr hub_topic another_topic{"hub.device.availability"};
constexpr std::string_view test_topic_name{"hub.device.test"};
constexpr hub_topic test_topic{test_topic_name};

void receive(void *arg, esp_event_base_t event_base, std::int32_t event_id, void *event_data) {
    if (event_base != HUB_EVENTS || event_id != reachability_topic.event_id || event_data == nullptr) {
        return;
    }
    auto &observed = *static_cast<observed_update *>(arg);
    const auto *event = static_cast<const reachability_event *>(event_data);
    observed.id_size = event->id_size;
    std::memcpy(observed.id.data(), event->id, event->id_size);
    observed.reachable = event->reachable;
    ++observed.count;
    xSemaphoreGive(observed.received);
}

void wait_for(const observed_update &observed) {
    REQUIRE(xSemaphoreTake(observed.received, pdMS_TO_TICKS(1'000)) == pdTRUE);
}

esp_err_t publish_reachability(hub_events &events, std::string_view id, bool reachable) {
    std::vector<std::byte> storage(sizeof(reachability_event) + id.size());
    auto *event = ::new (storage.data()) reachability_event{
        .id_size = id.size(),
        .reachable = reachable,
    };
    std::memcpy(event->id, id.data(), id.size());
    return events.publish(reachability_topic, storage);
}

}  // namespace

TEST_CASE("topics are evaluated at compile time") {
    STATIC_REQUIRE(reachability_topic.event_id == hub_topic_id_from_text("hub.device.reachability"));
    STATIC_REQUIRE(reachability_topic.event_id != another_topic.event_id);
    STATIC_REQUIRE(test_topic.event_id == hub_topic_id_from_text(test_topic_name));
}

TEST_CASE("reachability delivery copies payloads and unsubscribes") {
    observed_update first{.received = xSemaphoreCreateBinary()};
    observed_update second{.received = xSemaphoreCreateBinary()};
    REQUIRE(first.received != nullptr);
    REQUIRE(second.received != nullptr);

    hub_events events;
    auto first_registration = events.subscribe(reachability_topic, &receive, &first);
    REQUIRE(first_registration);
    const auto first_handler = *first_registration;

    {
        auto second_registration = events.subscribe(reachability_topic, &receive, &second);
        REQUIRE(second_registration);
        const auto second_handler = *second_registration;

        std::string source = "kitchen-light";
        REQUIRE(publish_reachability(events, source, true) == ESP_OK);
        source.assign("changed");

        wait_for(first);
        wait_for(second);
        CHECK(std::string_view(first.id.data(), first.id_size) == "kitchen-light");
        CHECK(std::string_view(second.id.data(), second.id_size) == "kitchen-light");
        CHECK(first.reachable);
        CHECK(second.reachable);
        REQUIRE(events.unsubscribe(reachability_topic, second_handler) == ESP_OK);
    }

    REQUIRE(publish_reachability(events, "living-sensor", false) == ESP_OK);
    wait_for(first);
    vTaskDelay(pdMS_TO_TICKS(100));
    CHECK(first.count == 2);
    CHECK(second.count == 1);
    REQUIRE(events.unsubscribe(reachability_topic, first_handler) == ESP_OK);

    vSemaphoreDelete(first.received);
    vSemaphoreDelete(second.received);
}

#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

consteval std::uint32_t hub_topic_rotate_left(std::uint32_t value, std::uint32_t amount) {
    return (value << amount) | (value >> (32U - amount));
}

consteval std::int32_t hub_topic_event_id(std::uint32_t hash) {
    if (hash == std::numeric_limits<std::uint32_t>::max()) {
        return -2;
    }
    if (hash <= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())) {
        return static_cast<std::int32_t>(hash);
    }
    return std::numeric_limits<std::int32_t>::min() + static_cast<std::int32_t>(hash - 0x8000'0000U);
}

// Compile time Murmur hash
consteval std::int32_t hub_topic_id_from_text(std::string_view name) {
    constexpr std::uint32_t c1 = 0xcc9e2d51U;
    constexpr std::uint32_t c2 = 0x1b873593U;
    constexpr std::uint32_t seed = 0U;
    const std::size_t length = name.size();
    std::uint32_t hash = seed;
    std::size_t index = 0;

    for (; index + 4 <= length; index += 4) {
        std::uint32_t block = static_cast<unsigned char>(name[index]) |
                              (static_cast<std::uint32_t>(static_cast<unsigned char>(name[index + 1])) << 8U) |
                              (static_cast<std::uint32_t>(static_cast<unsigned char>(name[index + 2])) << 16U) |
                              (static_cast<std::uint32_t>(static_cast<unsigned char>(name[index + 3])) << 24U);
        block *= c1;
        block = hub_topic_rotate_left(block, 15U);
        block *= c2;
        hash ^= block;
        hash = hub_topic_rotate_left(hash, 13U);
        hash = hash * 5U + 0xe6546b64U;
    }

    std::uint32_t tail = 0;
    if (index + 3 == length) {
        tail ^= static_cast<std::uint32_t>(static_cast<unsigned char>(name[index + 2])) << 16U;
    }
    if (index + 2 <= length) {
        tail ^= static_cast<std::uint32_t>(static_cast<unsigned char>(name[index + 1])) << 8U;
    }
    if (index < length) {
        tail ^= static_cast<unsigned char>(name[index]);
        tail *= c1;
        tail = hub_topic_rotate_left(tail, 15U);
        tail *= c2;
        hash ^= tail;
    }

    hash ^= length;
    hash ^= hash >> 16U;
    hash *= 0x85ebca6bU;
    hash ^= hash >> 13U;
    hash *= 0xc2b2ae35U;
    hash ^= hash >> 16U;
    return hub_topic_event_id(hash);
}

struct hub_topic {
    consteval explicit hub_topic(std::string_view name)
        : event_id(hub_topic_id_from_text(name)) {}

    std::int32_t event_id;
};

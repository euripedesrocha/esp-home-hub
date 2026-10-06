/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hub_web.hpp"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <new>
#include <string>

#include "esp_http_server.h"
#include "esp_timer.h"

namespace {

constexpr std::size_t MAX_BODY_SIZE = 4096;
constexpr std::int64_t MAX_BODY_RECEIVE_TIME_US = 5'000'000;

template <typename value_type, std::size_t size>
consteval std::array<char, size> embed_html(const value_type (&input)[size]) {
    std::array<char, size> output{};
    for (std::size_t index = 0; index < size; ++index) {
        output[index] = std::bit_cast<char>(static_cast<unsigned char>(input[index]));
    }
    return output;
}

constexpr auto INDEX_HTML = embed_html({
#embed "static/index.html"
});

esp_err_t send_system(httpd_req_t *request, hub_system &system) {
    try {
        const std::string body = system.json();
        httpd_resp_set_type(request, "application/json; charset=utf-8");
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        return httpd_resp_send(request, body.data(), static_cast<ssize_t>(body.size()));
    } catch (const std::bad_alloc &) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
}

}  // namespace

hub_web::hub_web(hub_system &model)
    : system(model) {}

hub_web::~hub_web() {
    stop();
}

esp_err_t hub_web::serve() {
    if (server != nullptr) {
        return ESP_OK;
    }

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t handle = nullptr;
    esp_err_t err = httpd_start(&handle, &config);
    if (err != ESP_OK) {
        return err;
    }
    server = handle;

    const httpd_uri_t root = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = &hub_web::get_root,
        .user_ctx = this,
    };
    const httpd_uri_t get_api = {
        .uri = "/api/system",
        .method = HTTP_GET,
        .handler = &hub_web::get_system,
        .user_ctx = this,
    };
    const httpd_uri_t post_api = {
        .uri = "/api/system",
        .method = HTTP_POST,
        .handler = &hub_web::post_system,
        .user_ctx = this,
    };
    err = httpd_register_uri_handler(handle, &root);
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(handle, &get_api);
    }
    if (err == ESP_OK) {
        err = httpd_register_uri_handler(handle, &post_api);
    }
    if (err != ESP_OK) {
        stop();
        return err;
    }

    return ESP_OK;
}

esp_err_t hub_web::stop() {
    if (server == nullptr) {
        return ESP_OK;
    }

    const esp_err_t err = httpd_stop(static_cast<httpd_handle_t>(server));
    if (err == ESP_OK) {
        server = nullptr;
    }
    return err;
}

esp_err_t hub_web::get_root(httpd_req_t *request) {
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, INDEX_HTML.data(), static_cast<ssize_t>(INDEX_HTML.size()));
}

esp_err_t hub_web::get_system(httpd_req_t *request) {
    auto *self = static_cast<hub_web *>(request->user_ctx);
    return send_system(request, self->system);
}

esp_err_t hub_web::post_system(httpd_req_t *request) {
    auto *self = static_cast<hub_web *>(request->user_ctx);
    if (request->content_len == 0) {
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Request body is required");
    }
    if (request->content_len > MAX_BODY_SIZE) {
        return httpd_resp_send_err(request, HTTPD_413_CONTENT_TOO_LARGE, "System document is too large");
    }

    try {
        std::string body(request->content_len, '\0');
        std::size_t received = 0;
        const std::int64_t deadline = esp_timer_get_time() + MAX_BODY_RECEIVE_TIME_US;
        while (received < body.size()) {
            if (esp_timer_get_time() >= deadline) {
                return httpd_resp_send_err(request, HTTPD_408_REQ_TIMEOUT, "Request body timed out");
            }
            const int result = httpd_req_recv(request, body.data() + received, body.size() - received);
            if (result == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            if (result <= 0) {
                return ESP_FAIL;
            }
            received += static_cast<std::size_t>(result);
        }

        const hub_system::replace_result result = self->system.replace(body);
        if (result != hub_system::replace_result::success) {
            return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "Invalid system document");
        }
        return send_system(request, self->system);
    } catch (const std::bad_alloc &) {
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    }
}

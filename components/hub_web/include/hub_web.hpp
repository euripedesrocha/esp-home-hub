/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "hub_system.hpp"

struct httpd_req;

class hub_web {
public:
    explicit hub_web(hub_system &system);
    ~hub_web();
    hub_web(const hub_web &) = delete ("HTTP resources are owned by this object");
    hub_web &operator=(const hub_web &) = delete ("HTTP resources are owned by this object");

    esp_err_t serve();
    esp_err_t stop();

private:
    static esp_err_t get_root(httpd_req *request);
    static esp_err_t get_system(httpd_req *request);
    static esp_err_t post_system(httpd_req *request);

    hub_system &system;
    void *server = nullptr;
};

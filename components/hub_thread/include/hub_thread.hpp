#pragma once

#include <mutex>

#include "hub_error.hpp"

class hub_thread {
public:
    hub_thread();
    ~hub_thread();
    hub_thread(const hub_thread &) = delete ("OpenThread resources are owned by this object");
    hub_thread &operator=(const hub_thread &) = delete ("OpenThread resources are owned by this object");

    esp_err_t start();
    esp_err_t stop();

private:
    bool started = false;
    std::mutex lifecycle;
};

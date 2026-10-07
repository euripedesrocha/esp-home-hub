/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    size_t id_size;
    bool reachable;
    char id[];
} hub_reachability_event_t;

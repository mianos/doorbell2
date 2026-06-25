#pragma once

#include <cstdint>
#include "esp_timer.h"

// millis()-equivalent for ESP-IDF (monotonic ms since boot).
static inline uint32_t radar_millis() {
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

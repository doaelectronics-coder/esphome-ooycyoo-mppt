#pragma once
#include <cstdint>
namespace esphome {
inline uint32_t host_millis = 0;
inline uint32_t millis() { return host_millis; }
}  // namespace esphome

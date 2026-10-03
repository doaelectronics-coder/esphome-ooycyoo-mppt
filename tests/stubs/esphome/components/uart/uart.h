#pragma once
// Host-test stand-in: bytes the component writes go to `sent`; bytes queued
// in `incoming` are what the controller replied.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>
namespace esphome { namespace uart {
class UARTComponent {};
class UARTDevice {
 public:
  std::vector<std::vector<uint8_t>> sent;
  std::deque<uint8_t> incoming;
  bool available() { return !incoming.empty(); }
  bool read_byte(uint8_t *value) {
    if (incoming.empty()) return false;
    *value = incoming.front(); incoming.pop_front(); return true;
  }
  void write_array(const uint8_t *data, size_t len) { sent.emplace_back(data, data + len); }
  void flush() {}
};
}}  // namespace esphome::uart

#pragma once
#include <cstdint>
#include <string>
namespace esphome {
inline uint32_t fnv1_hash(const std::string &str) {
  uint32_t hash = 2166136261UL;
  for (char c : str) { hash *= 16777619UL; hash ^= static_cast<uint8_t>(c); }
  return hash;
}
}  // namespace esphome

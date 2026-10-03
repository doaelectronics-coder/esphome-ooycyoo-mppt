#pragma once
// Host-test stand-in: preferences persist across component instances
// (a simulated reboot) in a process-wide map.
#include <cstdint>
#include <cstring>
#include <map>
#include <vector>
namespace esphome {
inline std::map<uint32_t, std::vector<uint8_t>> host_flash;
inline int host_sync_count = 0;
class ESPPreferenceObject {
 public:
  ESPPreferenceObject() = default;
  explicit ESPPreferenceObject(uint32_t key) : key_(key) {}
  template<typename T> bool save(const T *src) {
    const auto *bytes = reinterpret_cast<const uint8_t *>(src);
    host_flash[key_] = std::vector<uint8_t>(bytes, bytes + sizeof(T));
    return true;
  }
  template<typename T> bool load(T *dest) {
    auto it = host_flash.find(key_);
    if (it == host_flash.end() || it->second.size() != sizeof(T)) return false;
    std::memcpy(dest, it->second.data(), sizeof(T));
    return true;
  }
 private:
  uint32_t key_{0};
};
class ESPPreferences {
 public:
  template<typename T> ESPPreferenceObject make_preference(uint32_t key, bool) { return ESPPreferenceObject(key); }
  bool sync() { host_sync_count++; return true; }
};
inline ESPPreferences host_preferences;
inline ESPPreferences *global_preferences = &host_preferences;
}  // namespace esphome

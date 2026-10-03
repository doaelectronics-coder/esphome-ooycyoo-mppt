#pragma once
namespace esphome { namespace sensor {
class Sensor {
 public:
  void publish_state(float value) { state = value; has_state = true; }
  float state{0};
  bool has_state{false};
};
}}  // namespace esphome::sensor

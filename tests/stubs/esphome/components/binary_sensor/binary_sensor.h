#pragma once
namespace esphome { namespace binary_sensor {
class BinarySensor {
 public:
  void publish_state(bool value) { state = value; }
  bool state{false};
};
}}  // namespace esphome::binary_sensor

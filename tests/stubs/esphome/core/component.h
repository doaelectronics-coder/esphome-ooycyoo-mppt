#pragma once
// Host-test stand-in for ESPHome's component base classes.
namespace esphome {
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
};
class PollingComponent : public Component {
 public:
  virtual void update() {}
};
}  // namespace esphome

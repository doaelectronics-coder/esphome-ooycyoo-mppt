#pragma once
namespace esphome { namespace switch_ {
class Switch {
 public:
  virtual ~Switch() = default;
  void publish_state(bool value) { state = value; }
  bool state{false};
 protected:
  virtual void write_state(bool state) = 0;
};
}}  // namespace esphome::switch_

#pragma once
namespace esphome { namespace number {
class Number {
 public:
  virtual ~Number() = default;
  void publish_state(float value) { state = value; }
  void make_call_set(float value) { control(value); }
  float state{0};
 protected:
  virtual void control(float value) = 0;
};
}}  // namespace esphome::number

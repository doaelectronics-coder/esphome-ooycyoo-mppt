#pragma once

#include <algorithm>
#include <cmath>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/number/number.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"

namespace esphome {
namespace ooycyoo_mppt {

static const char *const TAG = "ooycyoo_mppt";
static const char *const COMPONENT_VERSION = "0.2.1";

class OoycyooMPPT;

class OoycyooLoadSwitch : public switch_::Switch {
 public:
  explicit OoycyooLoadSwitch(OoycyooMPPT *parent) : parent_(parent) {}

 protected:
  void write_state(bool state) override;
  OoycyooMPPT *parent_;
};

class OoycyooResetButton : public button::Button {
 public:
  explicit OoycyooResetButton(OoycyooMPPT *parent) : parent_(parent) {}

 protected:
  void press_action() override;
  OoycyooMPPT *parent_;
};

enum OoycyooSetting : uint8_t {
  SETTING_PV_OFF = 0,
  SETTING_LOAD_OFF = 1,
  SETTING_LOAD_ON = 2,
  SETTING_EVENING = 3,
  SETTING_INTERVAL = 4,
  SETTING_DAWN = 5,
};

class OoycyooSettingNumber : public number::Number {
 public:
  OoycyooSettingNumber(OoycyooMPPT *parent, uint8_t setting) : parent_(parent), setting_(setting) {}

 protected:
  void control(float value) override;
  OoycyooMPPT *parent_;
  uint8_t setting_;
};

class OoycyooMPPT : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override {
    ESP_LOGCONFIG(TAG, "OOYCYOO MPPT protocol component %s ready", COMPONENT_VERSION);
    // Requested settings survive a reboot. Without a saved copy (first boot on
    // 0.2.1) the controller's settings are adopted once three replies agree,
    // so the YAML defaults are never pushed to a controller unasked.
    this->pref_ = global_preferences->make_preference<SavedSettings>(fnv1_hash(this->store_key_), true);
    SavedSettings saved{};
    if (this->pref_.load(&saved) && saved.magic == SAVED_MAGIC) {
      for (uint8_t i = 0; i < SAVED_BYTES; i++)
        this->tx_[i] = saved.bytes[i];
      this->settings_known_ = true;
      ESP_LOGI(TAG, "Restored requested settings saved before the reboot");
    }
    this->publish_requested_settings_();
    if (this->setting_update_pending_ != nullptr)
      this->setting_update_pending_->publish_state(false);
    this->publish_write_attempts_();
  }

  void update() override {
    if (this->poll_enabled_)
      this->send_poll_();
  }

  void loop() override {
    uint8_t value;
    while (this->available() && this->read_byte(&value)) {
      if (this->rx_index_ == 0 && value != 0x55)
        continue;
      this->rx_[this->rx_index_++] = value;
      if (this->rx_index_ != FRAME_SIZE)
        continue;
      this->handle_frame_();
      this->rx_index_ = 0;
    }
  }

  void set_poll_enabled(bool enabled) { this->poll_enabled_ = enabled; }
  void set_auto_resync(bool enabled) { this->auto_resync_ = enabled; }
  void set_store_key(const std::string &key) { this->store_key_ = key; }
  void set_write_attempts_sensor(sensor::Sensor *s) { this->write_attempts_sensor_ = s; }

  void set_settings(float pv_off, float load_off, float load_on, uint8_t evening, uint8_t interval, uint8_t dawn) {
    this->put_be_(0, this->to_tenths_(pv_off));
    this->put_le_(3, this->to_tenths_(load_off));
    this->put_le_(5, this->to_tenths_(load_on));
    this->tx_[7] = evening;
    this->tx_[8] = interval;
    this->tx_[9] = dawn;
  }

  void request_load(bool desired_on) {
    if (desired_on == this->load_on_)
      return;
    this->toggle_pending_ = true;
  }

  void request_reset() {
    this->set_settings(13.8f, 10.7f, 12.6f, 24, 0, 0);
    // Reset uses its own R[11] sequence, so cancel any partly completed
    // ordinary R[10] setting commit before starting it.
    this->settings_write_pending_ = false;
    this->settings_polls_sent_ = 0;
    this->reset_phase_ = 1;
    this->reset_twos_remaining_ = 0;
    this->settings_ack_pending_ = true;
    // The controller's post-reset settings are adopted once the reset
    // sequence ends; the 12 V values above are never pushed by a resync.
    this->settings_known_ = false;
    this->adopt_matches_ = 0;
    this->write_attempts_ = 0;
    this->awaiting_confirmation_ = false;
    this->publish_requested_settings_();
    this->publish_setting_status_(false, true);
  }

  void set_setting_value(uint8_t setting, float value) {
    switch (setting) {
      case SETTING_PV_OFF: this->put_be_(0, this->to_tenths_(value)); break;
      case SETTING_LOAD_OFF: this->put_le_(3, this->to_tenths_(value)); break;
      case SETTING_LOAD_ON: this->put_le_(5, this->to_tenths_(value)); break;
      case SETTING_EVENING: this->tx_[7] = static_cast<uint8_t>(std::round(value)); break;
      case SETTING_INTERVAL: this->tx_[8] = static_cast<uint8_t>(std::round(value)); break;
      case SETTING_DAWN: this->tx_[9] = static_cast<uint8_t>(std::round(value)); break;
      default:
        ESP_LOGW(TAG, "Ignored unknown setting index %u", setting);
        return;
    }
    // The physical display sends nine ordinary setting polls followed by one
    // R[10]=1 commit poll. The controller acknowledges in the next reply.
    // A new request starts a fresh attempt count and cancels any wait.
    this->settings_known_ = true;
    this->write_attempts_ = 0;
    this->next_retry_ms_ = millis();
    this->save_settings_();
    this->start_write_();
    this->publish_requested_settings_();
    this->publish_setting_status_(false, true);
  }

  void set_pv_voltage_sensor(sensor::Sensor *s) { this->pv_voltage_ = s; }
  void set_pv_current_sensor(sensor::Sensor *s) { this->pv_current_ = s; }
  void set_solar_power_sensor(sensor::Sensor *s) { this->solar_power_ = s; }
  void set_battery_voltage_sensor(sensor::Sensor *s) { this->battery_voltage_ = s; }
  void set_battery_current_sensor(sensor::Sensor *s) { this->battery_current_ = s; }
  void set_battery_charging_power_sensor(sensor::Sensor *s) { this->battery_charging_power_ = s; }
  void set_battery_temperature_sensor(sensor::Sensor *s) { this->battery_temperature_ = s; }
  void set_battery_percent_sensor(sensor::Sensor *s) { this->battery_percent_ = s; }
  void set_load_voltage_sensor(sensor::Sensor *s) { this->load_voltage_ = s; }
  void set_load_current_sensor(sensor::Sensor *s) { this->load_current_ = s; }
  void set_total_energy_sensor(sensor::Sensor *s) { this->total_energy_ = s; }
  void set_load_path_fault_sensor(binary_sensor::BinarySensor *s) { this->load_path_fault_ = s; }
  void set_charge_path_fault_sensor(binary_sensor::BinarySensor *s) { this->charge_path_fault_ = s; }
  void set_settings_acknowledged_sensor(binary_sensor::BinarySensor *s) { this->settings_acknowledged_ = s; }
  void set_setting_update_pending_sensor(binary_sensor::BinarySensor *s) { this->setting_update_pending_ = s; }
  void set_load_switch(OoycyooLoadSwitch *s) { this->load_switch_ = s; }
  void set_setting_feedback_sensor(uint8_t setting, sensor::Sensor *s) {
    if (setting < SETTING_COUNT)
      this->setting_feedback_sensors_[setting] = s;
  }
  void set_setting_number(uint8_t setting, OoycyooSettingNumber *s) {
    if (setting >= SETTING_COUNT)
      return;
    this->setting_numbers_[setting] = s;
    this->publish_requested_settings_();
  }

 protected:
  static constexpr uint8_t PAYLOAD_SIZE = 30;
  static constexpr uint8_t FRAME_SIZE = 32;
  static constexpr uint8_t SETTING_COUNT = 6;
  uint8_t tx_[PAYLOAD_SIZE]{};
  uint8_t rx_[FRAME_SIZE]{};
  uint8_t rx_index_{0};
  bool poll_enabled_{false};
  bool initialization_pending_{true};
  bool load_on_{false};
  bool toggle_pending_{false};
  bool settings_write_pending_{false};
  bool settings_ack_pending_{false};
  uint8_t settings_polls_sent_{0};
  // Resync: out-of-sync replies seen while idle, replies since a commit,
  // commit attempts for the current request and when the next may start.
  static constexpr uint8_t SAVED_BYTES = 10;
  static constexpr uint8_t SAVED_MAGIC = 0xA5;
  static constexpr uint8_t STEADY_MISMATCH_REPLIES = 3;
  static constexpr uint8_t CONFIRMATION_REPLIES = 3;
  struct SavedSettings {
    uint8_t magic;
    uint8_t bytes[SAVED_BYTES];
  };
  ESPPreferenceObject pref_;
  std::string store_key_{"ooycyoo_mppt"};
  bool settings_known_{false};
  uint8_t adopt_matches_{0};
  uint8_t adopt_candidate_[SAVED_BYTES + 1]{};
  bool auto_resync_{true};
  bool awaiting_confirmation_{false};
  uint8_t mismatch_replies_{0};
  uint8_t replies_since_commit_{0};
  uint16_t write_attempts_{0};
  uint32_t next_retry_ms_{0};
  sensor::Sensor *write_attempts_sensor_{nullptr};
  uint8_t reset_phase_{0};
  uint8_t reset_twos_remaining_{0};

  sensor::Sensor *pv_voltage_{nullptr};
  sensor::Sensor *pv_current_{nullptr};
  sensor::Sensor *solar_power_{nullptr};
  sensor::Sensor *battery_voltage_{nullptr};
  sensor::Sensor *battery_current_{nullptr};
  sensor::Sensor *battery_charging_power_{nullptr};
  sensor::Sensor *battery_temperature_{nullptr};
  sensor::Sensor *battery_percent_{nullptr};
  sensor::Sensor *load_voltage_{nullptr};
  sensor::Sensor *load_current_{nullptr};
  sensor::Sensor *total_energy_{nullptr};
  sensor::Sensor *setting_feedback_sensors_[SETTING_COUNT]{};
  binary_sensor::BinarySensor *load_path_fault_{nullptr};
  binary_sensor::BinarySensor *charge_path_fault_{nullptr};
  binary_sensor::BinarySensor *settings_acknowledged_{nullptr};
  binary_sensor::BinarySensor *setting_update_pending_{nullptr};
  OoycyooLoadSwitch *load_switch_{nullptr};
  OoycyooSettingNumber *setting_numbers_[SETTING_COUNT]{};

  uint16_t to_tenths_(float value) const { return static_cast<uint16_t>(std::round(value * 10.0f)); }
  uint16_t be_(uint8_t offset) const { return (uint16_t(this->rx_[offset + 1]) << 8) | this->rx_[offset + 2]; }
  uint16_t le_(uint8_t offset) const { return uint16_t(this->rx_[offset + 1]) | (uint16_t(this->rx_[offset + 2]) << 8); }
  uint16_t tx_be_(uint8_t offset) const { return (uint16_t(this->tx_[offset]) << 8) | this->tx_[offset + 1]; }
  uint16_t tx_le_(uint8_t offset) const { return uint16_t(this->tx_[offset]) | (uint16_t(this->tx_[offset + 1]) << 8); }
  void put_be_(uint8_t offset, uint16_t value) { this->tx_[offset] = value >> 8; this->tx_[offset + 1] = value & 0xFF; }
  void put_le_(uint8_t offset, uint16_t value) { this->tx_[offset] = value & 0xFF; this->tx_[offset + 1] = value >> 8; }

  void send_poll_() {
    if (this->initialization_pending_) {
      uint8_t packet[FRAME_SIZE]{};
      packet[0] = 0x55;
      this->write_array(packet, FRAME_SIZE);
      this->flush();
      this->initialization_pending_ = false;
      ESP_LOGD(TAG, "Sent startup initialization poll");
      return;
    }

    const bool commit_settings = this->settings_write_pending_ && this->settings_polls_sent_ >= 9;
    this->tx_[10] = commit_settings ? 1 : 0;
    this->tx_[11] = this->reset_phase_;
    this->tx_[13] = this->toggle_pending_ ? 1 : 0;
    uint8_t packet[FRAME_SIZE];
    packet[0] = 0x55;
    uint8_t checksum = 0;
    for (uint8_t i = 0; i < PAYLOAD_SIZE; i++) {
      packet[i + 1] = this->tx_[i];
      checksum += this->tx_[i];
    }
    packet[31] = checksum;
    ESP_LOGD(TAG, "TX poll: PV-off=%02X%02X load-off=%02X%02X load-on=%02X%02X timers=%u/%u/%u commit=%02X flags=%02X/%02X checksum=%02X",
             this->tx_[0], this->tx_[1], this->tx_[3], this->tx_[4], this->tx_[5], this->tx_[6],
             this->tx_[7], this->tx_[8], this->tx_[9], this->tx_[10], this->tx_[11], this->tx_[13], checksum);
    this->write_array(packet, FRAME_SIZE);
    this->flush();

    if (this->toggle_pending_)
      this->toggle_pending_ = false;
    if (this->settings_write_pending_) {
      if (commit_settings) {
        this->settings_write_pending_ = false;
        this->settings_polls_sent_ = 0;
        this->awaiting_confirmation_ = true;
        this->replies_since_commit_ = 0;
      } else {
        this->settings_polls_sent_++;
      }
    }
    if (this->reset_phase_ == 1) {
      this->reset_phase_ = 2;
      this->reset_twos_remaining_ = 3;
    } else if (this->reset_phase_ == 2 && --this->reset_twos_remaining_ == 0) {
      this->reset_phase_ = 0;
    }
  }

  // Reply byte R[6] is not part of load-on: MPPT 1 answered FC EF to the
  // original display's FC 00 (25.2 V) in every 2026-09-10 capture, and EF never
  // followed a setting change. Load-on is compared on R[5] alone.
  bool settings_match_() const {
    return this->be_(0) == this->tx_be_(0) &&
           this->le_(3) == this->tx_le_(3) &&
           this->rx_[6] == this->tx_[5] &&
           this->rx_[8] == this->tx_[7] &&
           this->rx_[9] == this->tx_[8] &&
           this->rx_[10] == this->tx_[9];
  }

  void publish_setting_status_(bool acknowledged, bool pending) {
    if (this->settings_acknowledged_ != nullptr)
      this->settings_acknowledged_->publish_state(acknowledged);
    if (this->setting_update_pending_ != nullptr)
      this->setting_update_pending_->publish_state(pending);
  }

  void publish_controller_settings_() {
    const float values[SETTING_COUNT] = {
        this->be_(0) / 10.0f,
        this->le_(3) / 10.0f,
        this->reported_load_on_tenths_() / 10.0f,
        static_cast<float>(this->rx_[8]),
        static_cast<float>(this->rx_[9]),
        static_cast<float>(this->rx_[10]),
    };
    for (uint8_t i = 0; i < SETTING_COUNT; i++)
      if (this->setting_feedback_sensors_[i] != nullptr)
        this->setting_feedback_sensors_[i]->publish_state(values[i]);
  }

  void handle_frame_() {
    uint8_t checksum = 0;
    for (uint8_t i = 1; i <= PAYLOAD_SIZE; i++)
      checksum += this->rx_[i];
    if (checksum != this->rx_[31]) {
      ESP_LOGW(TAG, "Discarded frame with invalid checksum");
      return;
    }

    ESP_LOGD(TAG, "RX reply: settings=%02X%02X/%02X%02X/%02X%02X/%u/%u/%u checksum=%02X",
             this->rx_[1], this->rx_[2], this->rx_[4], this->rx_[5], this->rx_[6], this->rx_[7],
             this->rx_[8], this->rx_[9], this->rx_[10], this->rx_[31]);

    const float pv_v = this->be_(10) / 10.0f;
    const float battery_a = this->be_(12) / 10.0f;
    const float battery_v = this->be_(14) / 10.0f;
    const float temperature_c = this->be_(16) / 10.0f;
    const float load_a = this->be_(20) / 10.0f;
    const float total_kwh = this->be_(28) / 10.0f;
    this->load_on_ = this->rx_[19] == 0;

    float pv_a = 0.0f;
    if (pv_v >= battery_v + 1.0f && pv_v > 0.0f)
      pv_a = (battery_v * battery_a) / pv_v;
    const float solar_w = pv_v * pv_a;
    const float battery_charging_w = battery_v * battery_a;

    const float controller_load_off = this->le_(3) / 10.0f;
    const float controller_pv_off = this->be_(0) / 10.0f;
    float percent = 0.0f;
    if (controller_pv_off > controller_load_off)
      percent = std::max(0.0f, std::min(100.0f, 100.0f * (battery_v - controller_load_off) /
                                                       (controller_pv_off - controller_load_off)));

    if (this->pv_voltage_ != nullptr) this->pv_voltage_->publish_state(pv_v);
    if (this->pv_current_ != nullptr) this->pv_current_->publish_state(pv_a);
    if (this->solar_power_ != nullptr) this->solar_power_->publish_state(solar_w);
    if (this->battery_voltage_ != nullptr) this->battery_voltage_->publish_state(battery_v);
    if (this->battery_current_ != nullptr) this->battery_current_->publish_state(battery_a);
    if (this->battery_charging_power_ != nullptr) this->battery_charging_power_->publish_state(battery_charging_w);
    if (this->battery_temperature_ != nullptr) this->battery_temperature_->publish_state(temperature_c);
    if (this->battery_percent_ != nullptr) this->battery_percent_->publish_state(percent);
    if (this->load_voltage_ != nullptr) this->load_voltage_->publish_state(this->load_on_ ? battery_v : 0.0f);
    if (this->load_current_ != nullptr) this->load_current_->publish_state(load_a);
    if (this->total_energy_ != nullptr) this->total_energy_->publish_state(total_kwh);
    if (this->load_path_fault_ != nullptr) this->load_path_fault_->publish_state((this->rx_[23] & 0x01) != 0);
    if (this->charge_path_fault_ != nullptr) this->charge_path_fault_->publish_state((this->rx_[24] & 0x01) != 0);
    if (this->load_switch_ != nullptr) this->load_switch_->publish_state(this->load_on_);

    if (!this->settings_known_ && this->reset_phase_ == 0)
      this->consider_adopting_();
    this->publish_controller_settings_();
    ESP_LOGV(TAG, "Reply byte R[6]=%02X (not part of load-on)", this->rx_[7]);
    const bool acknowledged = this->settings_match_();
    this->update_sync_(acknowledged);
    this->publish_setting_status_(acknowledged, this->settings_ack_pending_);
  }

  uint16_t reported_load_on_tenths_() const {
    // Low byte from the controller; the high byte is not reported, so the
    // requested one is assumed.
    return uint16_t(this->rx_[6]) | (uint16_t(this->tx_[6]) << 8);
  }

  // Adopt only after the same settings arrive in consecutive replies.
  void consider_adopting_() {
    bool same = this->adopt_matches_ > 0;
    for (uint8_t i = 1; i <= SAVED_BYTES && same; i++)
      same = i == 7 || this->adopt_candidate_[i] == this->rx_[i];  // skip R[6]
    for (uint8_t i = 1; i <= SAVED_BYTES; i++)
      this->adopt_candidate_[i] = this->rx_[i];
    this->adopt_matches_ = same ? this->adopt_matches_ + 1 : 1;
    if (this->adopt_matches_ >= STEADY_MISMATCH_REPLIES)
      this->adopt_controller_settings_();
  }

  void adopt_controller_settings_() {
    this->tx_[0] = this->rx_[1];
    this->tx_[1] = this->rx_[2];
    this->tx_[3] = this->rx_[4];
    this->tx_[4] = this->rx_[5];
    this->tx_[5] = this->rx_[6];
    this->tx_[7] = this->rx_[8];
    this->tx_[8] = this->rx_[9];
    this->tx_[9] = this->rx_[10];
    this->settings_known_ = true;
    this->save_settings_();
    this->publish_requested_settings_();
    ESP_LOGI(TAG, "No saved settings: adopted the controller's current settings as requested");
  }

  void save_settings_() {
    SavedSettings saved{};
    saved.magic = SAVED_MAGIC;
    for (uint8_t i = 0; i < SAVED_BYTES; i++)
      saved.bytes[i] = this->tx_[i];
    this->pref_.save(&saved);
    // Write now: a reboot inside the usual flash delay would restore an
    // older request and resync the controller back to it.
    global_preferences->sync();
  }

  void start_write_() {
    this->settings_write_pending_ = true;
    this->settings_ack_pending_ = true;
    this->settings_polls_sent_ = 0;
    this->awaiting_confirmation_ = false;
    this->mismatch_replies_ = 0;
    this->write_attempts_++;
    this->publish_write_attempts_();
  }

  static uint32_t retry_delay_ms_(uint16_t attempts) {
    // Each attempt is a settings commit on the controller, so back off.
    if (attempts <= 1) return 30000;
    if (attempts == 2) return 120000;
    if (attempts == 3) return 600000;
    return 3600000;
  }

  void update_sync_(bool acknowledged) {
    if (acknowledged) {
      this->mismatch_replies_ = 0;
      this->awaiting_confirmation_ = false;
      if (this->settings_ack_pending_ && !this->settings_write_pending_) {
        this->settings_ack_pending_ = false;
        ESP_LOGI(TAG, "Controller acknowledged all requested settings");
      }
      if (this->write_attempts_ != 0 && !this->settings_write_pending_) {
        this->write_attempts_ = 0;
        this->publish_write_attempts_();
      }
      return;
    }
    if (this->settings_write_pending_ || this->reset_phase_ != 0 || !this->settings_known_)
      return;
    if (this->awaiting_confirmation_) {
      if (++this->replies_since_commit_ < CONFIRMATION_REPLIES)
        return;
      this->awaiting_confirmation_ = false;
      this->next_retry_ms_ = millis() + retry_delay_ms_(this->write_attempts_);
      ESP_LOGW(TAG, "Controller did not confirm the settings (attempt %u); retrying in %u s",
               this->write_attempts_, retry_delay_ms_(this->write_attempts_) / 1000);
      return;
    }
    this->settings_ack_pending_ = true;
    if (!this->auto_resync_ || ++this->mismatch_replies_ < STEADY_MISMATCH_REPLIES)
      return;
    if (static_cast<int32_t>(millis() - this->next_retry_ms_) < 0)
      return;
    ESP_LOGW(TAG, "Controller settings differ from the requested ones; resending (attempt %u)",
             this->write_attempts_ + 1);
    this->start_write_();
  }

  void publish_write_attempts_() {
    if (this->write_attempts_sensor_ != nullptr)
      this->write_attempts_sensor_->publish_state(this->write_attempts_);
  }

  void publish_requested_settings_() {
    const float values[SETTING_COUNT] = {
        this->tx_be_(0) / 10.0f,
        this->tx_le_(3) / 10.0f,
        this->tx_le_(5) / 10.0f,
        static_cast<float>(this->tx_[7]),
        static_cast<float>(this->tx_[8]),
        static_cast<float>(this->tx_[9]),
    };
    for (uint8_t i = 0; i < SETTING_COUNT; i++)
      if (this->setting_numbers_[i] != nullptr)
        this->setting_numbers_[i]->publish_state(values[i]);
  }
};

inline void OoycyooLoadSwitch::write_state(bool state) {
  this->parent_->request_load(state);
  this->publish_state(state);
}

inline void OoycyooResetButton::press_action() { this->parent_->request_reset(); }

inline void OoycyooSettingNumber::control(float value) {
  this->parent_->set_setting_value(this->setting_, value);
  this->publish_state(value);
}

}  // namespace ooycyoo_mppt
}  // namespace esphome

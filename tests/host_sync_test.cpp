// Host test for the OOYCYOO bridge's setting sync (0.2.1).
//
// Builds the real component header against small ESPHome stand-ins
// (tests/stubs) and drives it against a simulated controller that behaves
// like the 2026-09-10 captures: it echoes stored settings in every reply,
// answers load-on with a constant 0xEF in R[6], and stores a poll's settings
// only when that poll carries the R[10] = 1 commit flag.
//
//   g++ -std=c++17 -Wall -Wextra -Itests/stubs -Icomponents
//       tests/host_sync_test.cpp -o /tmp/ooycyoo_host_test && /tmp/ooycyoo_host_test

#include <cassert>
#include <cstdio>
#include <memory>
#include <string>

#include "ooycyoo_mppt/ooycyoo_mppt.h"

using namespace esphome;
using namespace esphome::ooycyoo_mppt;

struct Controller {
  uint8_t settings[10] = {0x01, 0x13, 0x00, 0xD6, 0x00, 0xFC, 0x00, 24, 0, 0};  // 27.5/21.4/25.2
  bool accept_commits = true;
  int commits_seen = 0;

  double pv_off() const { return ((settings[0] << 8) | settings[1]) / 10.0; }

  std::vector<uint8_t> reply() const {
    uint8_t payload[30] = {};
    for (int i = 0; i < 10; i++) payload[i] = settings[i];
    payload[6] = 0xEF;  // not part of load-on (captures)
    payload[10] = 1;
    payload[13] = 0x0A;               // 1.0 A charging
    payload[14] = 0x01; payload[15] = 0x09;  // 26.5 V battery
    std::vector<uint8_t> frame{0x55};
    uint8_t checksum = 0;
    for (uint8_t b : payload) { frame.push_back(b); checksum += b; }
    frame.push_back(checksum);
    return frame;
  }

  void receive(const std::vector<uint8_t> &poll) {
    bool zero = true;
    for (size_t i = 1; i < 31; i++) zero = zero && poll[i] == 0;
    if (zero) return;  // startup frame
    if (poll[11] == 1) {  // R[10] commit
      commits_seen++;
      if (accept_commits)
        for (int i = 0; i < 10; i++) settings[i] = poll[1 + i];
    }
  }
};

class Bridge : public OoycyooMPPT {
 public:
  using OoycyooMPPT::tx_;
};

struct Rig {
  std::unique_ptr<Bridge> bridge;
  Controller *controller;
  sensor::Sensor pv_off_feedback, load_on_feedback, attempts;
  binary_sensor::BinarySensor acknowledged, pending;
  std::unique_ptr<OoycyooSettingNumber> pv_off_number, load_on_number;

  explicit Rig(Controller *c, float yaml_pv_off = 27.5f) : controller(c) { boot(yaml_pv_off); }

  void boot(float yaml_pv_off = 27.5f) {
    bridge = std::make_unique<Bridge>();
    bridge->set_poll_enabled(true);
    bridge->set_store_key("ooycyoo_mppt_mppt_1");
    bridge->set_settings(yaml_pv_off, 21.4f, 25.2f, 24, 0, 0);
    bridge->set_setting_feedback_sensor(0, &pv_off_feedback);
    bridge->set_setting_feedback_sensor(2, &load_on_feedback);
    bridge->set_settings_acknowledged_sensor(&acknowledged);
    bridge->set_setting_update_pending_sensor(&pending);
    bridge->set_write_attempts_sensor(&attempts);
    pv_off_number = std::make_unique<OoycyooSettingNumber>(bridge.get(), 0);
    load_on_number = std::make_unique<OoycyooSettingNumber>(bridge.get(), 2);
    bridge->set_setting_number(0, pv_off_number.get());
    bridge->set_setting_number(2, load_on_number.get());
    bridge->setup();
  }

  // One 0.84 s poll and the controller's reply.
  void poll() {
    host_millis += 840;
    size_t before = bridge->sent.size();
    bridge->update();
    for (size_t i = before; i < bridge->sent.size(); i++) {
      controller->receive(bridge->sent[i]);
      for (uint8_t b : controller->reply()) bridge->incoming.push_back(b);
    }
    bridge->loop();
  }
  void run_seconds(double seconds) {
    for (int i = 0; i < static_cast<int>(seconds / 0.84); i++) poll();
  }
  int commits_sent() const {
    int n = 0;
    for (auto &frame : bridge->sent) n += frame[11] == 1;
    return n;
  }
  void set_pv_off(float value) { pv_off_number->make_call_set(value); }
};

static void fresh() {
  host_flash.clear();
  host_millis = 1000;
}

static void test_load_on_quirk_does_not_break_acknowledgement() {
  fresh();
  Controller c;
  Rig rig(&c);
  rig.run_seconds(5);
  assert(rig.acknowledged.state);
  assert(!rig.pending.state);
  assert(rig.load_on_feedback.state > 25.19f && rig.load_on_feedback.state < 25.21f);
  assert(rig.commits_sent() == 0);
  std::puts("ok  load-on reply byte R[6] is ignored; in sync with no write");
}

static void test_first_boot_adopts_the_controller_instead_of_yaml() {
  fresh();
  Controller c;
  c.settings[1] = 0x12;  // controller holds 27.4 V; YAML says 27.5 V
  Rig rig(&c);
  rig.run_seconds(10);
  assert(rig.commits_sent() == 0);
  assert(rig.pv_off_number->state > 27.39f && rig.pv_off_number->state < 27.41f);
  assert(rig.acknowledged.state);
  std::puts("ok  first boot adopts 27.4 V from the controller and writes nothing");
}

static void test_change_commits_after_nine_polls_and_is_acknowledged() {
  fresh();
  Controller c;
  Rig rig(&c);
  rig.run_seconds(3);
  rig.set_pv_off(27.6f);
  assert(rig.pending.state);
  rig.run_seconds(12);
  assert(c.commits_seen == 1);
  assert(c.pv_off() > 27.59 && c.pv_off() < 27.61);
  assert(rig.acknowledged.state && !rig.pending.state);
  assert(rig.attempts.state == 0);
  std::puts("ok  a change commits once and is acknowledged");
}

static void test_reboot_restores_the_request_and_does_not_rewrite() {
  fresh();
  Controller c;
  Rig rig(&c);
  rig.run_seconds(3);
  rig.set_pv_off(27.2f);
  rig.run_seconds(12);
  rig.boot(27.5f);  // reboot with the YAML default still 27.5 V
  size_t before = rig.bridge->sent.size();
  rig.run_seconds(30);
  assert(rig.pv_off_number->state > 27.19f && rig.pv_off_number->state < 27.21f);
  assert(rig.commits_sent() == 0 && rig.bridge->sent.size() > before);
  assert(c.pv_off() > 27.19 && c.pv_off() < 27.21);
  std::puts("ok  a reboot restores 27.2 V and never pushes the YAML 27.5 V");
}

static void test_out_of_sync_controller_is_resynced() {
  fresh();
  Controller c;
  Rig rig(&c);
  rig.run_seconds(3);
  c.settings[1] = 0x10;  // controller drifts to 27.2 V by itself
  rig.run_seconds(15);
  assert(c.pv_off() > 27.49 && c.pv_off() < 27.51);
  assert(rig.acknowledged.state);
  assert(c.commits_seen == 1);
  std::puts("ok  a controller found out of sync is written back to the request");
}

static void test_refused_writes_retry_with_backoff_then_recover() {
  fresh();
  Controller c;
  c.accept_commits = false;
  Rig rig(&c);
  rig.run_seconds(3);
  rig.set_pv_off(27.6f);
  rig.run_seconds(15);
  assert(c.commits_seen == 1 && rig.pending.state);
  rig.run_seconds(25);                      // before the 30 s wait ends
  assert(c.commits_seen == 1);
  rig.run_seconds(20);                      // retry 2 after ~30 s
  assert(c.commits_seen == 2);
  rig.run_seconds(100);
  assert(c.commits_seen == 2);              // waiting 120 s
  rig.run_seconds(40);
  assert(c.commits_seen == 3);
  rig.run_seconds(610);
  assert(c.commits_seen == 4);              // after 600 s
  rig.run_seconds(3000);
  assert(c.commits_seen == 4);              // then hourly
  assert(rig.attempts.state == 4);
  c.accept_commits = true;
  rig.run_seconds(700);
  assert(c.commits_seen == 5);
  assert(rig.acknowledged.state && !rig.pending.state && rig.attempts.state == 0);
  std::printf("ok  refused writes retried at 0/30/120/600 s then hourly (%d commits in ~76 min)\n", c.commits_seen);
}

static void test_reset_adopts_post_reset_settings_without_pushing_12v_values() {
  fresh();
  Controller c;
  Rig rig(&c);
  rig.run_seconds(3);
  rig.bridge->request_reset();
  c.settings[0] = 0x01; c.settings[1] = 0x14;  // controller's own post-reset 27.6 V
  rig.run_seconds(15);
  assert(rig.pv_off_number->state > 27.59f && rig.pv_off_number->state < 27.61f);
  assert(rig.commits_sent() == 0);
  assert(rig.acknowledged.state);
  std::puts("ok  after a reset the controller's settings are adopted, not 13.8 V");
}

static void test_timers_need_not_add_up_to_24() {
  fresh();
  Controller c;
  Rig rig(&c);
  rig.run_seconds(3);
  // Captured 2026-09-10: the display committed 24/2/2 (28 h) and the
  // controller acknowledged it.
  OoycyooSettingNumber evening(rig.bridge.get(), 3), interval(rig.bridge.get(), 4), dawn(rig.bridge.get(), 5);
  interval.make_call_set(2);
  dawn.make_call_set(2);
  rig.run_seconds(12);
  assert(c.settings[7] == 24 && c.settings[8] == 2 && c.settings[9] == 2);
  assert(rig.acknowledged.state && c.commits_seen == 1);
  evening.make_call_set(20);
  rig.run_seconds(12);
  assert(c.settings[7] == 20 && rig.acknowledged.state && c.commits_seen == 2);
  std::puts("ok  timers 24/2/2 and 20/2/2 are written and acknowledged; no sum rule");
}

static void test_auto_resync_can_be_switched_off() {
  fresh();
  Controller c;
  Rig rig(&c);
  rig.bridge->set_auto_resync(false);
  rig.run_seconds(3);
  c.settings[1] = 0x10;
  rig.run_seconds(30);
  assert(c.commits_seen == 0);
  assert(!rig.acknowledged.state && rig.pending.state);
  std::puts("ok  auto_resync: false only reports the mismatch");
}

int main() {
  test_load_on_quirk_does_not_break_acknowledgement();
  test_first_boot_adopts_the_controller_instead_of_yaml();
  test_change_commits_after_nine_polls_and_is_acknowledged();
  test_reboot_restores_the_request_and_does_not_rewrite();
  test_out_of_sync_controller_is_resynced();
  test_refused_writes_retry_with_backoff_then_recover();
  test_reset_adopts_post_reset_settings_without_pushing_12v_values();
  test_timers_need_not_add_up_to_24();
  test_auto_resync_can_be_switched_off();
  std::puts("all host sync tests passed");
  return 0;
}

// Device side of protocol v3: settings, command parsing, sound sequencing,
// knob filtering, the meeting schedule and alert stages, and the PWM levels
// the firmware writes. No SDK, no heap.
#pragma once

#include <cstddef>
#include <cstdint>

#include "auth.hpp"
#include "knob.hpp"
#include "schedule.hpp"
#include "sequencer.hpp"
#include "wifi.hpp"

namespace rma3 {

enum Meter : uint8_t { kMeterNone = 0, kMeterBright = 1, kMeterVol = 2, kMeterBoth = 3 };
enum Mode : uint8_t { kModeManual = 0, kModeCalendar = 1 };

// Where a byte came from. Each source has its own line buffer, so bytes from
// the network can never finish a line typed over USB (or the other way).
enum class Source : uint8_t { Usb = 0, Net = 1 };

struct Settings {  // boot defaults: ready for calendar alerts
  bool light = true;
  uint8_t r = 255, g = 255, b = 255;
  uint8_t sound = kWestminster;
  uint8_t meter = kMeterNone;
  uint8_t mode = kModeCalendar;
};

struct Tone {
  uint16_t hz = 0;           // 0 = silent
  uint16_t duty_permille = 0;  // 0..500 (never above 50 %)
};

// A Wi-Fi change requested over USB, for the firmware to carry out.
enum class WifiAction : uint8_t { None, Save, Clear };

class Device {
 public:
  static constexpr size_t kMaxLine = 96;
  static constexpr size_t kMaxReply = 80;  // "OK HI <nr> <proof>" = 71

  // White balance: red is far brighter than green/blue at the same current.
  static constexpr uint8_t kDefaultBalance[3] = {77, 255, 255};

  Device();

  // Feed one received byte; when it completes a non-empty line, writes the
  // NUL-terminated reply into out[kMaxReply] and returns true.
  bool feed(Source src, char c, uint32_t now_ms, char* out);
  // Advance sound and alerts. A disconnected host stops a manual preview but
  // never a calendar alert.
  void tick(uint32_t now_ms, bool host_connected);
  void knob_sample(uint16_t raw) { knob_.sample(raw); }

  // Network status from the firmware (for NET and the state line).
  void set_net(NetStatus st, const char* ip);
  // A pending WIFIGO / WIFICLR; creds valid for Save.
  WifiAction take_wifi_action(WifiCreds* creds);

  // Pairing. The firmware loads the saved key at boot (nullptr = none).
  void set_key(const uint8_t* key);
  bool has_key() const { return has_key_; }
  // A new network client: fresh random challenge, not yet trusted.
  void begin_net_session(const uint8_t nonce[kNonceLen]);
  bool net_authed() const { return net_authed_; }
  // A KEY sent over USB that differs from the current one, to save to flash.
  bool take_new_key(uint8_t out[kKeyLen]);

  // Outputs.
  uint16_t led_level(int ch) const;  // 0..65535, 65535 = fully on (ch 0=R,1=G,2=B)
  Tone tone() const;

  const Settings& settings() const { return s_; }
  uint8_t knob() const { return knob_.percent(); }
  bool playing() const { return seq_.playing(); }
  Stage stage() const { return sched_.stage(now_); }
  const Schedule& schedule() const { return sched_; }

 private:
  struct LineBuf {
    char line[kMaxLine + 1] = {};
    size_t len = 0;
    bool overflow = false;
  };
  void handle_line(Source src, char* line, uint32_t now_ms, char* reply);
  bool handle_wifi(Source src, char* line, char* reply);
  void write_state(char* reply) const;
  void stage_color(float rgb[3], float* level) const;

  Settings s_;
  uint8_t bal_[3];
  Sequencer seq_;
  bool alert_sound_ = false;  // the current sound is a calendar alert
  Knob knob_;
  Schedule sched_;
  uint32_t now_ = 0;
  LineBuf in_[2];

  NetStatus net_ = kNetNone;
  char ip_[16] = "-";
  WifiCreds staged_;
  WifiAction wifi_action_ = WifiAction::None;

  uint8_t key_[kKeyLen] = {};
  bool has_key_ = false;
  bool key_changed_ = false;
  uint8_t nr_[kNonceLen] = {};  // this connection's robot nonce
  uint8_t nm_[kNonceLen] = {};  // the Mac's nonce (from HI)
  bool hi_done_ = false;        // one HI per connection
  bool auth_tried_ = false;     // one AUTH per connection
  bool net_authed_ = false;
  uint8_t ks_[kKeyLen] = {};    // session key for line tags
  uint32_t counter_ = 0;        // next expected tag counter
  bool check_tag(char* line);   // strips a valid " #tag"; false if missing/wrong
};

// Exposed for tests: perceptual curves.
uint16_t gamma16(uint8_t v);         // (v/255)^2.2 * 65535
uint16_t volume_permille(uint8_t pct);  // 500 * (pct/100)^0.7, 0 -> 0
// Breathing for the "soon" stage: 0.35..1.0 over a 2 s period.
float breathe(uint32_t now_ms);

}  // namespace rma3

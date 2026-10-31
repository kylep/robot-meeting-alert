// Device side of protocol v2: settings, command parsing, sound sequencing,
// knob filtering, and the PWM levels the firmware writes. No SDK, no heap.
#pragma once

#include <cstddef>
#include <cstdint>

#include "knob.hpp"
#include "sequencer.hpp"

namespace rma2 {

enum Meter : uint8_t { kMeterNone = 0, kMeterBright = 1, kMeterVol = 2, kMeterBoth = 3 };

struct Settings {
  bool light = false;
  uint8_t r = 255, g = 255, b = 255;
  uint8_t sound = kWestminster;
  uint8_t meter = kMeterNone;
};

struct Tone {
  uint16_t hz = 0;           // 0 = silent
  uint16_t duty_permille = 0;  // 0..500 (never above 50 %)
};

class Device {
 public:
  static constexpr size_t kMaxLine = 32;
  static constexpr size_t kMaxReply = 40;

  // White balance: red is far brighter than green/blue at the same current.
  // Scale (x/255) per channel; tuned by eye with BAL, then baked in here.
  static constexpr uint8_t kDefaultBalance[3] = {77, 255, 255};

  Device();

  // Feed one received byte; when it completes a non-empty line, writes the
  // NUL-terminated reply into out[kMaxReply] and returns true.
  bool feed(char c, uint32_t now_ms, char* out);
  // Advance the sound; a disconnected host silences it.
  void tick(uint32_t now_ms, bool host_connected);
  void knob_sample(uint16_t raw) { knob_.sample(raw); }

  // Outputs.
  uint16_t led_level(int ch) const;  // 0..65535, 65535 = fully on (ch 0=R,1=G,2=B)
  Tone tone() const;

  const Settings& settings() const { return s_; }
  uint8_t knob() const { return knob_.percent(); }
  bool playing() const { return seq_.playing(); }

 private:
  void handle_line(uint32_t now_ms, char* reply);
  void write_state(char* reply) const;

  Settings s_;
  uint8_t bal_[3];
  Sequencer seq_;
  Knob knob_;
  char line_[kMaxLine + 1] = {};
  size_t len_ = 0;
  bool overflow_ = false;
};

// Exposed for tests: perceptual curves.
uint16_t gamma16(uint8_t v);         // (v/255)^2.2 * 65535
uint16_t volume_permille(uint8_t pct);  // 500 * (pct/100)^0.7, 0 -> 0

}  // namespace rma2

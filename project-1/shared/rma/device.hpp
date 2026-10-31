// Device-side protocol state machine. Shared by the firmware and host tests,
// so it must stay freestanding-friendly: no heap, no std::string, no exceptions.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rma {

struct Outputs {
  bool r = false;
  bool g = false;
  bool b = false;
  bool buzz = false;
};

class Device {
 public:
  static constexpr uint32_t kBuzzLeaseMs = 300;
  static constexpr size_t kMaxLine = 32;
  static constexpr size_t kMaxReply = 32;

  // Feeds one received byte. When it completes a non-empty line, writes the
  // NUL-terminated reply (without newline) into out[kMaxReply] and returns true.
  bool feed(char c, uint32_t now_ms, char* out);

  // Expires the buzzer lease. A disconnected host silences the buzzer but
  // leaves the LEDs alone, so one-shot CLI commands keep their effect.
  void tick(uint32_t now_ms, bool host_connected);

  Outputs outputs() const { return out_; }

 private:
  void handle_line(uint32_t now_ms, char* reply);
  void write_state(char* reply) const;

  Outputs out_;
  uint32_t buzz_deadline_ = 0;
  char line_[kMaxLine + 1] = {};
  size_t len_ = 0;
  bool overflow_ = false;
};

}  // namespace rma

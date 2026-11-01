// Turns noisy 12-bit ADC readings of the potentiometer into a steady 0-100 %.
#pragma once

#include <cstdint>

namespace rma3 {

class Knob {
 public:
  // Real pots never quite reach the rails; anything inside these dead zones
  // counts as fully off / fully on.
  static constexpr int kLowCounts = 60;
  static constexpr int kHighCounts = 4035;

  void sample(uint16_t raw);  // call every ~10 ms
  uint8_t percent() const { return pct_; }

 private:
  float avg_ = -1.0f;  // exponential moving average of raw counts
  uint8_t pct_ = 0;
};

}  // namespace rma3

#include "knob.hpp"

#include <cmath>

namespace rma2 {

void Knob::sample(uint16_t raw) {
  avg_ = (avg_ < 0) ? raw : avg_ + (raw - avg_) * 0.25f;
  if (avg_ <= kLowCounts) {
    pct_ = 0;
    return;
  }
  if (avg_ >= kHighCounts) {
    pct_ = 100;
    return;
  }
  float pct = (avg_ - kLowCounts) * 100.0f / (kHighCounts - kLowCounts);
  // Hysteresis: only move when the reading has really moved a whole percent.
  if (std::fabs(pct - pct_) >= 1.0f) pct_ = static_cast<uint8_t>(std::lround(pct));
}

}  // namespace rma2

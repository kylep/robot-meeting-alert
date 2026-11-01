// Plays a Melody note by note against a millisecond clock. Each note sounds
// for its length minus a short gap, so repeated notes are distinct and the
// buzzer's frequency can change while it is silent.
#pragma once

#include <cstdint>

#include "melodies.hpp"

namespace rma3 {

class Sequencer {
 public:
  static constexpr uint16_t kGapMs = 25;

  void start(uint8_t sound, uint32_t now_ms);  // restarts; kOff stops
  void stop();
  void tick(uint32_t now_ms);                   // advance to the current note
  bool playing() const { return playing_; }
  uint16_t hz() const { return hz_; }           // 0 = silent right now

 private:
  bool playing_ = false;
  const Melody* mel_ = nullptr;
  uint32_t note_start_ = 0;
  uint32_t idx_ = 0;
  uint16_t hz_ = 0;
};

}  // namespace rma3

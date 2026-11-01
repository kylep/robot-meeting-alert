#include "sequencer.hpp"

namespace rma3 {

void Sequencer::start(uint8_t sound, uint32_t now_ms) {
  const Melody& m = melody(sound);
  if (m.count == 0) {
    stop();
    return;
  }
  mel_ = &m;
  idx_ = 0;
  note_start_ = now_ms;
  playing_ = true;
  tick(now_ms);
}

void Sequencer::stop() {
  playing_ = false;
  mel_ = nullptr;
  hz_ = 0;
}

void Sequencer::tick(uint32_t now_ms) {
  if (!playing_) return;
  // Wrap-safe elapsed time; skip any notes that finished while we were away.
  while (idx_ < mel_->count && now_ms - note_start_ >= mel_->notes[idx_].ms) {
    note_start_ += mel_->notes[idx_].ms;
    ++idx_;
  }
  if (idx_ >= mel_->count) {
    stop();
    return;
  }
  const Note& n = mel_->notes[idx_];
  uint32_t into = now_ms - note_start_;
  hz_ = (into + kGapMs < n.ms) ? n.hz : 0;
}

}  // namespace rma3

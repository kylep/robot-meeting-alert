// The robot's own copy of the next meetings, and the alert stages it shows.
// Times arrive as seconds relative to the CAL line and are kept on the
// device's millisecond clock, so the robot needs no wall clock. All
// comparisons are wrap-safe (the ms clock wraps every ~49 days).
#pragma once

#include <cstdint>

namespace rma3 {

enum Stage : uint8_t { kStageNone = 0, kStageFree = 1, kStageSoon = 2, kStageMeeting = 3 };

struct Meeting {
  uint16_t id = 0;
  uint32_t start = 0;  // device ms
  uint32_t end = 0;
};

class Schedule {
 public:
  static constexpr int kMax = 8;
  static constexpr int kRemember = 16;           // recently alerted ids
  static constexpr int32_t kSoonMs = 5 * 60 * 1000;
  static constexpr int32_t kLateMs = 60 * 1000;   // a start seen later than this stays silent
  static constexpr int32_t kFreshMs = 10 * 1000;  // full red for the first 10 s of a meeting
  static constexpr int32_t kMaxOffsetS = 86400;

  enum Result { kOk, kNoCal, kFull, kBadArg };

  Result begin(uint32_t now_ms);                         // CAL
  Result add(uint16_t id, int32_t start_s, int32_t end_s);  // EV
  Result commit(uint32_t now_ms, uint32_t valid_s);      // CALGO

  // Advances time. Returns true exactly once per meeting, when its melody
  // should start.
  bool tick(uint32_t now_ms);

  Stage stage(uint32_t now_ms) const;
  bool fresh_start(uint32_t now_ms) const;  // a meeting began < 10 s ago
  int count() const { return n_; }
  bool valid(uint32_t now_ms) const;

 private:
  bool alerted(uint16_t id) const;
  void remember(uint16_t id);
  void mark_late(uint32_t now_ms);

  Meeting m_[kMax];
  int n_ = 0;
  bool committed_ = false;
  uint32_t valid_until_ = 0;

  bool staging_ = false;
  uint32_t base_ = 0;
  Meeting stage_[kMax];
  int stage_n_ = 0;

  uint16_t done_[kRemember] = {};
  int done_next_ = 0;
};

}  // namespace rma3

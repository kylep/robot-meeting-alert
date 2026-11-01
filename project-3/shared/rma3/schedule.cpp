#include "schedule.hpp"

namespace rma3 {
namespace {

// Signed distance a - b on the wrapping millisecond clock.
int32_t diff(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b); }

}  // namespace

Schedule::Result Schedule::begin(uint32_t now_ms) {
  staging_ = true;
  base_ = now_ms;
  stage_n_ = 0;
  return kOk;
}

Schedule::Result Schedule::add(uint16_t id, int32_t start_s, int32_t end_s) {
  if (!staging_) return kNoCal;
  if (id == 0 || start_s < -kMaxOffsetS || start_s > kMaxOffsetS || end_s < -kMaxOffsetS ||
      end_s > kMaxOffsetS || start_s >= end_s) {
    return kBadArg;
  }
  if (stage_n_ == kMax) return kFull;
  Meeting& m = stage_[stage_n_++];
  m.id = id;
  m.start = base_ + static_cast<uint32_t>(start_s * 1000);
  m.end = base_ + static_cast<uint32_t>(end_s * 1000);
  return kOk;
}

Schedule::Result Schedule::commit(uint32_t now_ms, uint32_t valid_s) {
  if (!staging_) return kNoCal;
  if (valid_s < 60 || valid_s > 3600) return kBadArg;
  for (int i = 0; i < stage_n_; ++i) m_[i] = stage_[i];
  n_ = stage_n_;
  staging_ = false;
  committed_ = true;
  valid_until_ = now_ms + valid_s * 1000;
  mark_late(now_ms);
  return kOk;
}

bool Schedule::valid(uint32_t now_ms) const {
  return committed_ && diff(valid_until_, now_ms) > 0;
}

bool Schedule::alerted(uint16_t id) const {
  for (uint16_t d : done_) {
    if (d == id) return true;
  }
  return false;
}

void Schedule::remember(uint16_t id) {
  done_[done_next_] = id;
  done_next_ = (done_next_ + 1) % kRemember;
}

// Meetings that started too long ago are marked as done without a sound.
void Schedule::mark_late(uint32_t now_ms) {
  for (int i = 0; i < n_; ++i) {
    if (!alerted(m_[i].id) && diff(now_ms, m_[i].start) > kLateMs) remember(m_[i].id);
  }
}

bool Schedule::tick(uint32_t now_ms) {
  if (!valid(now_ms)) return false;
  mark_late(now_ms);
  bool fire = false;
  for (int i = 0; i < n_; ++i) {
    if (!alerted(m_[i].id) && diff(now_ms, m_[i].start) >= 0) {
      remember(m_[i].id);
      fire = true;
    }
  }
  return fire;
}

Stage Schedule::stage(uint32_t now_ms) const {
  if (!valid(now_ms)) return kStageNone;
  bool soon = false;
  for (int i = 0; i < n_; ++i) {
    int32_t to_start = diff(m_[i].start, now_ms);
    if (to_start <= 0 && diff(m_[i].end, now_ms) > 0) return kStageMeeting;
    if (to_start > 0 && to_start <= kSoonMs) soon = true;
  }
  return soon ? kStageSoon : kStageFree;
}

bool Schedule::fresh_start(uint32_t now_ms) const {
  if (!valid(now_ms)) return false;
  for (int i = 0; i < n_; ++i) {
    int32_t since = diff(now_ms, m_[i].start);
    if (since >= 0 && since < kFreshMs && diff(m_[i].end, now_ms) > 0) return true;
  }
  return false;
}

}  // namespace rma3

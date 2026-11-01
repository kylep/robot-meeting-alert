// Host-side protocol v2 helpers (Mac app, CLI, tests). Not linked into firmware.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rma3 {

struct State {
  bool light = false;
  uint8_t r = 0, g = 0, b = 0;
  uint8_t sound = 0;
  uint8_t meter = 0;
  uint8_t knob = 0;
  bool playing = false;
  uint8_t mode = 0;
  uint8_t stage = 0;
  uint8_t net = 0;
};

// A calendar event as the Mac sees it (times in seconds since 1970).
struct CalEvent {
  std::string id;  // the calendar's own event identifier (never sent)
  double start = 0, end = 0;
  bool all_day = false, declined = false, free = false, cancelled = false;
};

// One EV line: id, start and end in whole seconds from `now`.
struct Pushed {
  uint16_t id = 0;
  int32_t start = 0, end = 0;
};

// Which meetings the robot should know about: next 12 h, no all-day,
// declined, free or cancelled events; running ones first, then upcoming by
// start; at most 8 (the device's limit). Offsets clamped to +-86400.
std::vector<Pushed> pick_meetings(double now, const std::vector<CalEvent>& events);

// 1..65535 from the event identifier and start (no title involved).
uint16_t meeting_id(const std::string& event_id, double start);

// Accepts exactly "OK L<0|1> C<RRGGBB> S<0-10> M<0-3> K<0-100> Z<0|1> T<0|1> A<0-3> W<0-3>".
bool parse_state(std::string_view line, State* out);

// Accepts "OK NET W<0-3> <ip or ->".
bool parse_net(std::string_view line, int* status, std::string* ip);

// Accepts "RMA 3 <version>".
bool parse_hello(std::string_view line, std::string* fw_version);

}  // namespace rma3

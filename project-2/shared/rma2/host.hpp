// Host-side protocol v2 helpers (Mac app, CLI, tests). Not linked into firmware.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace rma2 {

struct State {
  bool light = false;
  uint8_t r = 0, g = 0, b = 0;
  uint8_t sound = 0;
  uint8_t meter = 0;
  uint8_t knob = 0;
  bool playing = false;
};

// Accepts exactly "OK L<0|1> C<RRGGBB> S<0-4> M<0-3> K<0-100> Z<0|1>".
bool parse_state(std::string_view line, State* out);

// Accepts "RMA 2 <version>".
bool parse_hello(std::string_view line, std::string* fw_version);

}  // namespace rma2

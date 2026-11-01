// Sound data: public-domain tunes, played two octaves up (about 1-2 kHz),
// where the kit's small passive buzzer is loudest.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rma3 {

struct Note {
  uint16_t hz;  // 0 = rest
  uint16_t ms;
};

struct Melody {
  const char* name;
  const Note* notes;
  size_t count;
};

enum Sound : uint8_t { kOff = 0, kWestminster = 1, kTwinkle = 2, kOde = 3, kBeep = 4, kSoundCount = 5 };

// melody(kOff) has no notes.
const Melody& melody(uint8_t sound);

}  // namespace rma3

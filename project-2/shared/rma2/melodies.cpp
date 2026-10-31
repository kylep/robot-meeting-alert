#include "melodies.hpp"

namespace rma2 {
namespace {

// Note frequencies (Hz), equal temperament, A4 = 440.
constexpr uint16_t B5 = 988, C6 = 1047, D6 = 1175, E6 = 1319, F6 = 1397,
                   Fs6 = 1480, G6 = 1568, Gs6 = 1661, A6 = 1760;

// Westminster Quarters (the Big Ben chime), changes 1 and 2:
// G# F# E B, E G# F# B. Three short notes and a long one each.
constexpr Note kWestminsterNotes[] = {
    {Gs6, 300}, {Fs6, 300}, {E6, 300}, {B5, 600},
    {E6, 300},  {Gs6, 300}, {Fs6, 300}, {B5, 600},
};

// Twinkle Twinkle Little Star, first line: C C G G A A G.
constexpr Note kTwinkleNotes[] = {
    {C6, 375}, {C6, 375}, {G6, 375}, {G6, 375}, {A6, 375}, {A6, 375}, {G6, 750},
};

// Ode to Joy (Beethoven), opening: E E F G G F E D.
constexpr Note kOdeNotes[] = {
    {E6, 330}, {E6, 330}, {F6, 330}, {G6, 330}, {G6, 330}, {F6, 330}, {E6, 330}, {D6, 690},
};

constexpr Note kBeepNotes[] = {{2000, 300}};

constexpr Melody kMelodies[kSoundCount] = {
    {"Off", nullptr, 0},
    {"Westminster chime", kWestminsterNotes, sizeof kWestminsterNotes / sizeof(Note)},
    {"Twinkle twinkle", kTwinkleNotes, sizeof kTwinkleNotes / sizeof(Note)},
    {"Ode to joy", kOdeNotes, sizeof kOdeNotes / sizeof(Note)},
    {"Beep", kBeepNotes, 1},
};

}  // namespace

const Melody& melody(uint8_t sound) {
  return kMelodies[sound < kSoundCount ? sound : 0];
}

}  // namespace rma2

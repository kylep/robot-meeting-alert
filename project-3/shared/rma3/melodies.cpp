#include "melodies.hpp"

namespace rma3 {
namespace {

// Note frequencies (Hz), equal temperament, A4 = 440.
constexpr uint16_t A5 = 880, B5 = 988, C6 = 1047, D6 = 1175, Ds6 = 1245, E6 = 1319, F6 = 1397,
                   Fs6 = 1480, G6 = 1568, Gs6 = 1661, A6 = 1760, B6 = 1976, C7 = 2093;

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

// Für Elise (Beethoven), opening, one octave up: E D# E D# E B D C A.
constexpr Note kFurEliseNotes[] = {
    {E6, 250}, {Ds6, 250}, {E6, 250}, {Ds6, 250}, {E6, 250}, {B5, 250}, {D6, 250}, {C6, 250}, {A5, 1000},
};

// Frère Jacques (traditional): C D E C, C D E C.
constexpr Note kFrereNotes[] = {
    {C6, 375}, {D6, 375}, {E6, 375}, {C6, 375}, {C6, 375}, {D6, 375}, {E6, 375}, {C6, 375},
};

// Mary Had a Little Lamb (traditional): E D C D E E E.
constexpr Note kMaryNotes[] = {
    {E6, 375}, {D6, 375}, {C6, 375}, {D6, 375}, {E6, 375}, {E6, 375}, {E6, 750},
};

// Happy Birthday (public domain), first line: G G A G C B, in 3/4.
constexpr Note kBirthdayNotes[] = {
    {G6, 375}, {G6, 125}, {A6, 500}, {G6, 500}, {C7, 500}, {B6, 1000},
};

// Ding-dong doorbell.
constexpr Note kDoorbellNotes[] = {{G6, 700}, {E6, 1100}};

// A quick rising chirp: C E G C.
constexpr Note kChirpNotes[] = {{C6, 150}, {E6, 150}, {G6, 150}, {C7, 400}};

constexpr Melody kMelodies[kSoundCount] = {
    {"Off", nullptr, 0},
    {"Westminster chime", kWestminsterNotes, sizeof kWestminsterNotes / sizeof(Note)},
    {"Twinkle twinkle", kTwinkleNotes, sizeof kTwinkleNotes / sizeof(Note)},
    {"Ode to joy", kOdeNotes, sizeof kOdeNotes / sizeof(Note)},
    {"Beep", kBeepNotes, 1},
    {"Fur Elise", kFurEliseNotes, sizeof kFurEliseNotes / sizeof(Note)},
    {"Frere Jacques", kFrereNotes, sizeof kFrereNotes / sizeof(Note)},
    {"Mary had a little lamb", kMaryNotes, sizeof kMaryNotes / sizeof(Note)},
    {"Happy birthday", kBirthdayNotes, sizeof kBirthdayNotes / sizeof(Note)},
    {"Doorbell", kDoorbellNotes, sizeof kDoorbellNotes / sizeof(Note)},
    {"Chirp", kChirpNotes, sizeof kChirpNotes / sizeof(Note)},
};

}  // namespace

const Melody& melody(uint8_t sound) {
  return kMelodies[sound < kSoundCount ? sound : 0];
}

}  // namespace rma3

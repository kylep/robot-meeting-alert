// The robot's light and beep settings: a colour for each calendar status, the
// warning beep, the start-of-meeting sound and overall volume and brightness.
// Set with VOL, BRIGHT, WARN, ONSTART and COL; read back with CFG.
#pragma once

#include <cstdint>

namespace rma3 {

// What the robot can be showing in Calendar mode. kLookStart is the first
// 10 s of a meeting; kLookWaiting is "no calendar from the Mac (yet, or for
// 15 minutes)".
enum Look : uint8_t { kLookWaiting = 0, kLookFree = 1, kLookSoon = 2, kLookStart = 3, kLookMeeting = 4, kLooks = 5 };

// none = entering this status leaves the light as it was.
struct LookColour {
  bool none = false;
  uint8_t r = 0, g = 0, b = 0;
};

// Same light on the robot (both "none", or the same colour).
inline bool same_look(const LookColour& a, const LookColour& b) {
  return a.none == b.none && (a.none || (a.r == b.r && a.g == b.g && a.b == b.b));
}

// Each default's brightness is folded into its colour: the robot treats a
// colour's level perceptually, so green at 40 % is exactly (0, 102, 0).
constexpr LookColour kDefaultLooks[kLooks] = {
    {false, 0, 0, 38},     // dim blue
    {false, 0, 102, 0},    // green
    {false, 255, 120, 0},  // amber, breathing
    {false, 255, 0, 0},    // red
    {false, 64, 0, 0},     // dim red
};

constexpr uint8_t kMaxWarnMin = 60;

struct Config {
  uint8_t volume = 100;      // %; multiplied by the knob when it controls volume
  uint8_t brightness = 100;  // %; multiplied by the knob when it controls brightness
  uint8_t warn_min = 0;      // beep this many minutes before each meeting; 0 = never
  bool start_sound = true;   // play the alert sound when a meeting starts
  LookColour look[kLooks] = {kDefaultLooks[0], kDefaultLooks[1], kDefaultLooks[2], kDefaultLooks[3],
                             kDefaultLooks[4]};
};

}  // namespace rma3

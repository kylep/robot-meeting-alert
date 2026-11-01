// GPIO numbers (not physical pin numbers). See ../docs/assembly.md.
#pragma once

namespace pins {
constexpr unsigned kRed = 10;     // physical pin 14, PWM slice 5 A
constexpr unsigned kGreen = 11;   // physical pin 15, PWM slice 5 B
constexpr unsigned kBlue = 12;    // physical pin 16, PWM slice 6 A
constexpr unsigned kBuzzer = 15;  // physical pin 20, PWM slice 7 B (keep GP14 unused: same slice)
constexpr unsigned kKnob = 26;    // physical pin 31, ADC input 0
constexpr unsigned kKnobAdcInput = 0;
}  // namespace pins

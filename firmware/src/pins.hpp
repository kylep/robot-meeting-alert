// GPIO numbers (not physical pin numbers). See docs/assembly.md.
#pragma once

namespace pins {
constexpr unsigned kRed = 10;     // physical pin 14
constexpr unsigned kGreen = 11;   // physical pin 15
constexpr unsigned kBlue = 12;    // physical pin 16
constexpr unsigned kBuzzer = 15;  // physical pin 20, drives S8050 base via 1k
}  // namespace pins

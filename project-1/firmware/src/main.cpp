// USB-serial controlled LEDs + buzzer. Protocol logic lives in shared/rma.
#include <cstdio>

#include "pico/stdlib.h"
#include "pins.hpp"
#include "rma/device.hpp"

namespace {

constexpr unsigned kPins[] = {pins::kRed, pins::kGreen, pins::kBlue, pins::kBuzzer};

rma::Device g_dev;

uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }

// Applies lease/disconnect rules and drives the GPIOs.
void service() {
  g_dev.tick(now_ms(), stdio_usb_connected());
  rma::Outputs o = g_dev.outputs();
  gpio_put(pins::kRed, o.r);
  gpio_put(pins::kGreen, o.g);
  gpio_put(pins::kBlue, o.b);
  gpio_put(pins::kBuzzer, o.buzz);
}

}  // namespace

int main() {
  for (unsigned pin : kPins) {
    gpio_init(pin);
    gpio_put(pin, false);  // drive low before enabling output: no boot glitch
    gpio_set_dir(pin, GPIO_OUT);
  }
  stdio_init_all();

  char reply[rma::Device::kMaxReply];
  for (;;) {
    service();
    // Bounded drain so a flood of input can't starve lease enforcement.
    for (int n = 0; n < 64; ++n) {
      int c = getchar_timeout_us(0);
      if (c == PICO_ERROR_TIMEOUT) break;
      if (g_dev.feed(static_cast<char>(c), now_ms(), reply)) {
        service();  // apply before replying
        std::printf("%s\n", reply);  // may block up to the stdout timeout
        service();  // re-check the lease after a possible block
      }
    }
    sleep_us(500);
  }
}

// Project 2: RGB light, melodies on a passive buzzer, and a knob.
// All behaviour lives in shared/rma2; this file only drives the hardware.
#include <cstdio>

#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/pwm.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"
#include "pins.hpp"
#include "rma2/device.hpp"

namespace {

constexpr unsigned kLedPins[3] = {pins::kRed, pins::kGreen, pins::kBlue};

rma2::Device g_dev;
uint16_t g_led[3] = {0, 0, 0};
rma2::Tone g_tone;

uint32_t now_ms() { return to_ms_since_boot(get_absolute_time()); }

// Common-anode RGB LED: a colour is on while its pin is LOW. Inverting the PWM
// output makes level 65535 mean "fully on" and level 0 (pin high) "off".
void led_init() {
  for (unsigned pin : kLedPins) {
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_config cfg = pwm_get_default_config();
    pwm_config_set_wrap(&cfg, 65535);  // 150 MHz / 65536 = ~2.3 kHz, no visible flicker
    pwm_config_set_clkdiv_int(&cfg, 1);
    pwm_config_set_output_polarity(&cfg, true, true);
    pwm_init(slice, &cfg, false);
    pwm_set_chan_level(slice, pwm_gpio_to_channel(pin), 0);
  }
  for (unsigned pin : kLedPins) pwm_set_enabled(pwm_gpio_to_slice_num(pin), true);
  for (unsigned pin : kLedPins) gpio_set_function(pin, GPIO_FUNC_PWM);  // pins go high: off
}

void buzzer_init() {
  uint slice = pwm_gpio_to_slice_num(pins::kBuzzer);
  pwm_config cfg = pwm_get_default_config();
  pwm_init(slice, &cfg, false);
  pwm_set_chan_level(slice, pwm_gpio_to_channel(pins::kBuzzer), 0);
  pwm_set_enabled(slice, true);
  gpio_set_function(pins::kBuzzer, GPIO_FUNC_PWM);
}

// Integer clock divider (fractional dividers make notes wobble), wrap sized
// so one PWM period is one cycle of the note.
void buzzer_set(const rma2::Tone& t) {
  uint slice = pwm_gpio_to_slice_num(pins::kBuzzer);
  uint chan = pwm_gpio_to_channel(pins::kBuzzer);
  pwm_set_chan_level(slice, chan, 0);  // silence first; change pitch while quiet
  if (t.hz == 0 || t.duty_permille == 0) return;
  uint32_t sys = clock_get_hz(clk_sys);
  uint32_t div = (sys + 65536u * t.hz - 1) / (65536u * t.hz);
  if (div < 1) div = 1;
  if (div > 255) div = 255;
  uint32_t wrap = (sys + div * t.hz / 2) / (div * t.hz) - 1;
  if (wrap > 65535) wrap = 65535;
  pwm_set_clkdiv_int_frac(slice, div, 0);
  pwm_set_wrap(slice, wrap);
  uint32_t duty = t.duty_permille > 500 ? 500 : t.duty_permille;  // never above 50 %
  pwm_set_chan_level(slice, chan, static_cast<uint16_t>((wrap + 1) * duty / 1000));
}

void apply_outputs() {
  for (int ch = 0; ch < 3; ++ch) {
    uint16_t level = g_dev.led_level(ch);
    if (level != g_led[ch]) {
      g_led[ch] = level;
      pwm_set_chan_level(pwm_gpio_to_slice_num(kLedPins[ch]), pwm_gpio_to_channel(kLedPins[ch]), level);
    }
  }
  rma2::Tone t = g_dev.tone();
  if (t.hz != g_tone.hz || t.duty_permille != g_tone.duty_permille) {
    g_tone = t;
    buzzer_set(t);
  }
}

void service() {
  g_dev.tick(now_ms(), stdio_usb_connected());
  apply_outputs();
  watchdog_update();
}

}  // namespace

int main() {
  // Buzzer pin low before anything else so the coil never sees DC.
  gpio_init(pins::kBuzzer);
  gpio_put(pins::kBuzzer, false);
  gpio_set_dir(pins::kBuzzer, GPIO_OUT);

  led_init();
  buzzer_init();
  adc_init();
  adc_gpio_init(pins::kKnob);
  adc_select_input(pins::kKnobAdcInput);
  stdio_init_all();
  watchdog_enable(500, true);  // a hung firmware resets, which releases the buzzer

  char reply[rma2::Device::kMaxReply];
  uint32_t next_knob = 0;
  for (;;) {
    uint32_t t = now_ms();
    if (static_cast<int32_t>(t - next_knob) >= 0) {
      g_dev.knob_sample(adc_read());
      next_knob = t + 10;
    }
    service();
    for (int n = 0; n < 64; ++n) {  // bounded drain per iteration
      int c = getchar_timeout_us(0);
      if (c == PICO_ERROR_TIMEOUT) break;
      if (g_dev.feed(static_cast<char>(c), now_ms(), reply)) {
        service();
        std::printf("%s\n", reply);  // may block up to 20 ms
        service();
      }
    }
    sleep_us(500);
  }
}

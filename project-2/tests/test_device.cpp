// Protocol v2, levels, sequencer and knob: the device logic, on the host.
#include <cstdint>
#include <cstring>
#include <string>

#include "check.hpp"
#include "rma2/device.hpp"
#include "rma2/host.hpp"
#include "rma2/melodies.hpp"

using rma2::Device;

namespace {

std::string send(Device& d, const std::string& s, uint32_t now = 0) {
  char out[Device::kMaxReply];
  std::string last;
  for (char c : s) {
    if (d.feed(c, now, out)) last = out;
  }
  return last;
}

void test_commands() {
  Device d;
  CHECK_STR(send(d, "HELLO\n"), "RMA 2 0.2.0");
  CHECK_STR(send(d, "STATE\n"), "OK L0 CFFFFFF S1 M0 K0 Z0");
  CHECK_STR(send(d, "LIGHT 1\n"), "OK L1 CFFFFFF S1 M0 K0 Z0");
  CHECK_STR(send(d, "RGB 255 128 0\n"), "OK L1 CFF8000 S1 M0 K0 Z0");
  CHECK_STR(send(d, "SOUND 3\n"), "OK L1 CFF8000 S3 M0 K0 Z0");  // no playback
  CHECK_STR(send(d, "METER 2\n"), "OK L1 CFF8000 S3 M2 K0 Z0");
  CHECK_STR(send(d, "PLAY\n"), "OK L1 CFF8000 S3 M2 K0 Z1");
  CHECK_STR(send(d, "STOP\n"), "OK L1 CFF8000 S3 M2 K0 Z0");
  CHECK_STR(send(d, "PLAY 4\n"), "OK L1 CFF8000 S3 M2 K0 Z1");  // preview keeps S3
  CHECK_STR(send(d, "PLAY 0\n"), "OK L1 CFF8000 S3 M2 K0 Z0");  // Off = stop
  CHECK_STR(send(d, "LIGHT 0\n"), "OK L0 CFF8000 S3 M2 K0 Z0");  // colour kept
}

void test_errors() {
  Device d;
  for (const char* s : {"RGB 256 0 0\n", "RGB 1 2\n", "RGB -1 0 0\n", "RGB 1 2 3 4\n", "SOUND 5\n",
                        "METER 4\n", "PLAY 5\n", "LIGHT 2\n", "LIGHT\n", "STOP 1\n", "BAL 1 2\n",
                        "RGB 0x1 0 0\n", "RGB 0001 0 0\n"}) {
    CHECK_STR(send(d, s), "ERR BADARG");
  }
  CHECK_STR(send(d, "light 1\n"), "ERR BADCMD");
  CHECK_STR(send(d, "A B C D E F G\n"), "ERR BADCMD");
  CHECK_STR(send(d, std::string(33, 'X') + "\n"), "ERR TOOLONG");
  CHECK_STR(send(d, "STATE\r\n"), "OK L0 CFFFFFF S1 M0 K0 Z0");
  CHECK_STR(send(d, "  \n"), "");
}

void test_worst_case_reply_round_trips() {
  Device d;
  send(d, "LIGHT 1\n");
  send(d, "SOUND 4\n");
  send(d, "METER 3\n");
  for (int i = 0; i < 40; ++i) d.knob_sample(4095);
  std::string r = send(d, "PLAY\n");
  CHECK_STR(r, "OK L1 CFFFFFF S4 M3 K100 Z1");
  CHECK(r.size() < Device::kMaxReply);
  rma2::State s;
  CHECK(rma2::parse_state(r, &s));
  CHECK(s.light && s.r == 255 && s.sound == 4 && s.meter == 3 && s.knob == 100 && s.playing);
}

void test_levels() {
  CHECK(rma2::gamma16(0) == 0);
  CHECK(rma2::gamma16(255) == 65535);
  CHECK(rma2::gamma16(128) > 13000 && rma2::gamma16(128) < 15000);  // ~0.218
  Device d;
  CHECK(d.led_level(0) == 0);  // light off
  send(d, "LIGHT 1\n");
  send(d, "BAL 255 255 255\n");
  send(d, "RGB 255 0 128\n");
  CHECK(d.led_level(0) == 65535);
  CHECK(d.led_level(1) == 0);
  CHECK(d.led_level(2) == rma2::gamma16(128));
  // Default balance tames red.
  Device e;
  send(e, "LIGHT 1\n");
  CHECK(e.led_level(0) < e.led_level(1) / 3 + 1);
  // Brightness mode follows the knob; None ignores it.
  Device k;
  send(k, "LIGHT 1\n");
  send(k, "BAL 255 255 255\n");
  for (int i = 0; i < 60; ++i) k.knob_sample(60 + (4035 - 60) / 2);  // ~50 %
  CHECK(k.led_level(1) == 65535);
  send(k, "METER 1\n");
  uint16_t half = k.led_level(1);
  CHECK(half > 13000 && half < 15500);  // perceptual half (0.5^2.2)
  send(k, "METER 3\n");
  CHECK(k.led_level(1) == half);
  send(k, "METER 2\n");
  CHECK(k.led_level(1) == 65535);
}

void test_volume() {
  CHECK(rma2::volume_permille(0) == 0);
  CHECK(rma2::volume_permille(100) == 500);
  CHECK(rma2::volume_permille(30) > 200);  // not nearly silent at 30 %
  Device d;
  send(d, "PLAY 4\n", 0);
  d.tick(10, true);
  CHECK(d.tone().hz == 2000 && d.tone().duty_permille == 500);
  send(d, "METER 2\n", 10);
  for (int i = 0; i < 40; ++i) d.knob_sample(0);
  CHECK(d.tone().hz == 0);  // knob at 0 in volume mode: silent (Kyle: 0 is an option)
  send(d, "METER 1\n", 10);
  CHECK(d.tone().hz == 2000 && d.tone().duty_permille == 500);  // brightness only
}

void test_sequencer() {
  using rma2::Sequencer;
  for (uint8_t s = 1; s <= 3; ++s) {
    const rma2::Melody& m = rma2::melody(s);
    uint32_t total = 0;
    for (size_t i = 0; i < m.count; ++i) total += m.notes[i].ms;
    CHECK(total >= 2950 && total <= 3050);
  }
  Sequencer q;
  q.start(rma2::kTwinkle, 1000);
  CHECK(q.playing() && q.hz() == 1047);           // C6
  q.tick(1000 + 375 - 10);
  CHECK(q.hz() == 0);                             // gap before the repeated C
  q.tick(1000 + 375 + 5);
  CHECK(q.hz() == 1047);                          // second C
  q.tick(1000 + 750 + 5);
  CHECK(q.hz() == 1568);                          // G6
  q.tick(1000 + 3000);
  CHECK(!q.playing() && q.hz() == 0);             // finished, silent
  q.start(rma2::kBeep, 0);
  q.start(rma2::kBeep, 200);                      // restart
  q.tick(450);
  CHECK(q.playing() && q.hz() == 2000);
  q.start(rma2::kOff, 460);
  CHECK(!q.playing());
  // Wraparound.
  q.start(rma2::kBeep, UINT32_MAX - 100);
  q.tick(100);
  CHECK(q.playing());
  q.tick(250);
  CHECK(!q.playing());
}

void test_disconnect_silences() {
  Device d;
  send(d, "LIGHT 1\n");
  send(d, "PLAY 1\n", 0);
  d.tick(10, false);
  CHECK(!d.playing() && d.tone().hz == 0);
  CHECK(d.settings().light);  // light persists
}

void test_knob() {
  rma2::Knob k;
  for (int i = 0; i < 30; ++i) k.sample(2048);
  uint8_t mid = k.percent();
  CHECK(mid >= 49 && mid <= 51);
  for (int i = 0; i < 200; ++i) k.sample(static_cast<uint16_t>(2048 + ((i % 2) ? 20 : -20)));
  CHECK(k.percent() == mid);                      // noise doesn't move it
  for (int i = 0; i < 40; ++i) k.sample(40);
  CHECK(k.percent() == 0);                        // real pot end reaches 0
  for (int i = 0; i < 40; ++i) k.sample(4060);
  CHECK(k.percent() == 100);                      // and 100
}

}  // namespace

int main() {
  test_commands();
  test_errors();
  test_worst_case_reply_round_trips();
  test_levels();
  test_volume();
  test_sequencer();
  test_disconnect_silences();
  test_knob();
  if (g_failures == 0) std::printf("test_device: ok\n");
  return g_failures;
}

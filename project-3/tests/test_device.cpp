// Protocol v3, levels, sequencer and knob: the device logic, on the host.
#include <cstdint>
#include <cstring>
#include <string>

#include "check.hpp"
#include "rma3/device.hpp"
#include "rma3/host.hpp"
#include "rma3/melodies.hpp"

using rma3::Device;

namespace {

using rma3::Source;

std::string send(Device& d, const std::string& s, uint32_t now = 0, Source src = Source::Usb) {
  char out[Device::kMaxReply];
  std::string last;
  for (char c : s) {
    if (d.feed(src, c, now, out)) last = out;
  }
  return last;
}

// Project 2 behaviour: manual mode, light starting off.
void manual(Device& d) {
  send(d, "MODE 0\n");
  send(d, "LIGHT 0\n");
}

void test_commands() {
  Device d;
  manual(d);
  CHECK_STR(send(d, "HELLO\n"), "RMA 3 0.3.0");
  CHECK_STR(send(d, "STATE\n"), "OK L0 CFFFFFF S1 M0 K0 Z0 T0 A0 W0");
  CHECK_STR(send(d, "LIGHT 1\n"), "OK L1 CFFFFFF S1 M0 K0 Z0 T0 A0 W0");
  CHECK_STR(send(d, "RGB 255 128 0\n"), "OK L1 CFF8000 S1 M0 K0 Z0 T0 A0 W0");
  CHECK_STR(send(d, "SOUND 3\n"), "OK L1 CFF8000 S3 M0 K0 Z0 T0 A0 W0");  // no playback
  CHECK_STR(send(d, "METER 2\n"), "OK L1 CFF8000 S3 M2 K0 Z0 T0 A0 W0");
  CHECK_STR(send(d, "PLAY\n"), "OK L1 CFF8000 S3 M2 K0 Z1 T0 A0 W0");
  CHECK_STR(send(d, "STOP\n"), "OK L1 CFF8000 S3 M2 K0 Z0 T0 A0 W0");
  CHECK_STR(send(d, "PLAY 4\n"), "OK L1 CFF8000 S3 M2 K0 Z1 T0 A0 W0");  // preview keeps S3
  CHECK_STR(send(d, "PLAY 0\n"), "OK L1 CFF8000 S3 M2 K0 Z0 T0 A0 W0");  // Off = stop
  CHECK_STR(send(d, "LIGHT 0\n"), "OK L0 CFF8000 S3 M2 K0 Z0 T0 A0 W0");  // colour kept
}

void test_errors() {
  Device d;
  manual(d);
  for (const char* s : {"RGB 256 0 0\n", "RGB 1 2\n", "RGB -1 0 0\n", "RGB 1 2 3 4\n", "SOUND 5\n",
                        "METER 4\n", "PLAY 5\n", "LIGHT 2\n", "LIGHT\n", "STOP 1\n", "BAL 1 2\n",
                        "RGB 0x1 0 0\n", "RGB 0001 0 0\n"}) {
    CHECK_STR(send(d, s), "ERR BADARG");
  }
  CHECK_STR(send(d, "light 1\n"), "ERR BADCMD");
  CHECK_STR(send(d, "A B C D E F G\n"), "ERR BADCMD");
  CHECK_STR(send(d, std::string(97, 'X') + "\n"), "ERR TOOLONG");
  CHECK_STR(send(d, "STATE\r\n"), "OK L0 CFFFFFF S1 M0 K0 Z0 T0 A0 W0");
  CHECK_STR(send(d, "  \n"), "");
}

void test_worst_case_reply_round_trips() {
  Device d;
  d.set_net(rma3::kNetFailed, "-");
  send(d, "CAL\n");
  send(d, "EV 7 -10 600\n");
  send(d, "CALGO 600\n");
  d.tick(0, true);
  send(d, "LIGHT 1\n");
  send(d, "SOUND 4\n");
  send(d, "METER 3\n");
  for (int i = 0; i < 40; ++i) d.knob_sample(4095);
  std::string r = send(d, "PLAY\n");
  CHECK_STR(r, "OK L1 CFFFFFF S4 M3 K100 Z1 T1 A3 W3");
  CHECK(r.size() < Device::kMaxReply);
  rma3::State s;
  CHECK(rma3::parse_state(r, &s));
  CHECK(s.light && s.r == 255 && s.sound == 4 && s.meter == 3 && s.knob == 100 && s.playing);
  CHECK(s.mode == 1 && s.stage == 3 && s.net == 3);
  CHECK(!rma3::parse_state("OK L1 CFFFFFF S4 M3 K100 Z1", &s));  // v2 line rejected
}

void test_levels() {
  CHECK(rma3::gamma16(0) == 0);
  CHECK(rma3::gamma16(255) == 65535);
  CHECK(rma3::gamma16(128) > 13000 && rma3::gamma16(128) < 15000);  // ~0.218
  Device d;
  manual(d);
  CHECK(d.led_level(0) == 0);  // light off
  send(d, "LIGHT 1\n");
  send(d, "BAL 255 255 255\n");
  send(d, "RGB 255 0 128\n");
  CHECK(d.led_level(0) == 65535);
  CHECK(d.led_level(1) == 0);
  CHECK(d.led_level(2) == rma3::gamma16(128));
  // Default balance tames red.
  Device e;
  manual(e);
  send(e, "LIGHT 1\n");
  CHECK(e.led_level(0) < e.led_level(1) / 3 + 1);
  // Brightness mode follows the knob; None ignores it.
  Device k;
  manual(k);
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
  CHECK(rma3::volume_permille(0) == 0);
  CHECK(rma3::volume_permille(100) == 500);
  CHECK(rma3::volume_permille(30) > 200);  // not nearly silent at 30 %
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
  using rma3::Sequencer;
  for (uint8_t s = 1; s <= 3; ++s) {
    const rma3::Melody& m = rma3::melody(s);
    uint32_t total = 0;
    for (size_t i = 0; i < m.count; ++i) total += m.notes[i].ms;
    CHECK(total >= 2950 && total <= 3050);
  }
  Sequencer q;
  q.start(rma3::kTwinkle, 1000);
  CHECK(q.playing() && q.hz() == 1047);           // C6
  q.tick(1000 + 375 - 10);
  CHECK(q.hz() == 0);                             // gap before the repeated C
  q.tick(1000 + 375 + 5);
  CHECK(q.hz() == 1047);                          // second C
  q.tick(1000 + 750 + 5);
  CHECK(q.hz() == 1568);                          // G6
  q.tick(1000 + 3000);
  CHECK(!q.playing() && q.hz() == 0);             // finished, silent
  q.start(rma3::kBeep, 0);
  q.start(rma3::kBeep, 200);                      // restart
  q.tick(450);
  CHECK(q.playing() && q.hz() == 2000);
  q.start(rma3::kOff, 460);
  CHECK(!q.playing());
  // Wraparound.
  q.start(rma3::kBeep, UINT32_MAX - 100);
  q.tick(100);
  CHECK(q.playing());
  q.tick(250);
  CHECK(!q.playing());
}

void test_disconnect_silences() {
  Device d;
  manual(d);
  send(d, "LIGHT 1\n");
  send(d, "PLAY 1\n", 0);
  d.tick(10, false);
  CHECK(!d.playing() && d.tone().hz == 0);
  CHECK(d.settings().light);  // light persists
}

void test_knob() {
  rma3::Knob k;
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

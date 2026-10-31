#include <cstdint>
#include <string>

#include "check.hpp"
#include "rma/device.hpp"

using rma::Device;

namespace {

// Feeds a whole string; returns the last reply, "" if none, "<multi>" if >1.
std::string send(Device& d, const std::string& s, uint32_t now = 0) {
  char out[Device::kMaxReply];
  std::string last;
  int replies = 0;
  for (char c : s) {
    if (d.feed(c, now, out)) {
      last = out;
      ++replies;
    }
  }
  return replies > 1 ? "<multi>" : last;
}

void test_commands() {
  Device d;
  CHECK_STR(send(d, "HELLO\n"), "RMA 1 0.1.0");
  CHECK_STR(send(d, "STATE\n"), "OK R0 G0 B0 Z0");
  CHECK_STR(send(d, "LED R 1\n"), "OK R1 G0 B0 Z0");
  CHECK_STR(send(d, "LED G 1\n"), "OK R1 G1 B0 Z0");
  CHECK_STR(send(d, "LED B 1\n"), "OK R1 G1 B1 Z0");
  CHECK_STR(send(d, "LED G 0\n"), "OK R1 G0 B1 Z0");
  CHECK_STR(send(d, "BUZZ 1\n"), "OK R1 G0 B1 Z1");
  CHECK_STR(send(d, "BUZZ 0\n"), "OK R1 G0 B1 Z0");
  CHECK_STR(send(d, "ALLOFF\n"), "OK R0 G0 B0 Z0");
}

void test_errors() {
  Device d;
  CHECK_STR(send(d, "led r 1\n"), "ERR BADCMD");
  CHECK_STR(send(d, "FOO\n"), "ERR BADCMD");
  CHECK_STR(send(d, "LED X 1\n"), "ERR BADARG");
  CHECK_STR(send(d, "LED R 2\n"), "ERR BADARG");
  CHECK_STR(send(d, "LED R\n"), "ERR BADARG");
  CHECK_STR(send(d, "LED RG 1\n"), "ERR BADARG");
  CHECK_STR(send(d, "BUZZ\n"), "ERR BADARG");
  CHECK_STR(send(d, "BUZZ 1 2\n"), "ERR BADARG");
  CHECK_STR(send(d, "HELLO X\n"), "ERR BADARG");
  CHECK_STR(send(d, "A B C D E\n"), "ERR BADCMD");
  CHECK(!d.outputs().r && !d.outputs().buzz);
}

void test_framing() {
  Device d;
  CHECK_STR(send(d, "\n"), "");
  CHECK_STR(send(d, "   \n"), "");
  CHECK_STR(send(d, "STATE\r\n"), "OK R0 G0 B0 Z0");
  CHECK_STR(send(d, "  LED  R   1  \n"), "OK R1 G0 B0 Z0");
  CHECK_STR(send(d, "\tSTATE\t\n"), "OK R1 G0 B0 Z0");
  // 32 chars is allowed (it is an unknown command, not too long).
  CHECK_STR(send(d, std::string(32, 'X') + "\n"), "ERR BADCMD");
  CHECK_STR(send(d, std::string(33, 'X') + "\n"), "ERR TOOLONG");
  CHECK_STR(send(d, "STATE\n"), "OK R1 G0 B0 Z0");
  // Partial line then completion across calls.
  CHECK_STR(send(d, "LED "), "");
  CHECK_STR(send(d, "G 1\n"), "OK R1 G1 B0 Z0");
}

void test_lease() {
  Device d;
  send(d, "BUZZ 1\n", 1000);
  d.tick(1299, true);
  CHECK(d.outputs().buzz);
  d.tick(1300, true);
  CHECK(!d.outputs().buzz);

  send(d, "BUZZ 1\n", 2000);
  send(d, "BUZZ 1\n", 2200);  // extends to 2500
  d.tick(2499, true);
  CHECK(d.outputs().buzz);
  d.tick(2500, true);
  CHECK(!d.outputs().buzz);
}

void test_lease_wraparound() {
  Device d;
  uint32_t t = UINT32_MAX - 100;
  send(d, "BUZZ 1\n", t);
  d.tick(t + 150, true);  // wrapped past 0
  CHECK(d.outputs().buzz);
  d.tick(t + 300, true);
  CHECK(!d.outputs().buzz);
}

void test_disconnect() {
  Device d;
  send(d, "LED R 1\n", 0);
  send(d, "LED B 1\n", 0);
  send(d, "BUZZ 1\n", 0);
  d.tick(10, false);
  rma::Outputs o = d.outputs();
  CHECK(!o.buzz);
  CHECK(o.r && !o.g && o.b);  // LEDs survive disconnect
}

}  // namespace

int main() {
  test_commands();
  test_errors();
  test_framing();
  test_lease();
  test_lease_wraparound();
  test_disconnect();
  if (g_failures == 0) std::printf("test_device: ok\n");
  return g_failures;
}

// Phase 3 logic on the host: schedule and alert stages, the new protocol
// commands, USB-only Wi-Fi commands, the Wi-Fi supervisor and flash record,
// and the Mac's meeting picker.
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "check.hpp"
#include "rma3/device.hpp"
#include "rma3/host.hpp"
#include "rma3/schedule.hpp"
#include "rma3/wifi.hpp"

using rma3::Device;
using rma3::Source;

namespace {

std::string send(Device& d, const std::string& s, uint32_t now = 0, Source src = Source::Usb) {
  char out[Device::kMaxReply];
  std::string last;
  for (char c : s) {
    if (d.feed(src, c, now, out)) last = out;
  }
  return last;
}

bool ok(const std::string& r) { return r.rfind("OK ", 0) == 0; }

constexpr uint32_t kS = 1000;  // ms per second

// One meeting `start_s` from `now`, lasting `dur_s`, valid 15 min.
void push(Device& d, uint32_t now, uint16_t id, int32_t start_s, int32_t dur_s) {
  CHECK(ok(send(d, "CAL\n", now)));
  CHECK(ok(send(d, "EV " + std::to_string(id) + " " + std::to_string(start_s) + " " +
                       std::to_string(start_s + dur_s) + "\n", now)));
  CHECK(ok(send(d, "CALGO 900\n", now)));
}

// Runs the device from `from` to `to` in 10 ms steps; counts melody starts.
int run(Device& d, uint32_t from, uint32_t to, bool host = true) {
  int starts = 0;
  bool was = d.playing();
  for (uint32_t t = from; static_cast<int32_t>(to - t) > 0; t += 10) {
    d.tick(t, host);
    if (d.playing() && !was) ++starts;
    was = d.playing();
  }
  return starts;
}

void test_new_commands() {
  Device d;
  CHECK_STR(send(d, "STATE\n"), "OK L1 CFFFFFF S1 M0 K0 Z0 T1 A0 W0");  // boot: calendar mode, no schedule
  CHECK_STR(send(d, "MODE 0\n"), "OK L1 CFFFFFF S1 M0 K0 Z0 T0 A0 W0");
  CHECK_STR(send(d, "MODE 2\n"), "ERR BADARG");
  CHECK_STR(send(d, "EV 1 10 20\n"), "ERR NOCAL");
  CHECK_STR(send(d, "CALGO 600\n"), "ERR NOCAL");
  CHECK(ok(send(d, "CAL\n")));
  for (const char* s : {"EV 0 10 20\n", "EV 65536 10 20\n", "EV 1 20 10\n", "EV 1 10 10\n",
                        "EV 1 -86401 10\n", "EV 1 10 86401\n", "EV 1 1x 20\n", "EV 1 10\n",
                        "CALGO 59\n", "CALGO 3601\n"}) {
    CHECK_STR(send(d, s), "ERR BADARG");
  }
  for (int i = 1; i <= 8; ++i) CHECK(ok(send(d, "EV " + std::to_string(i) + " -86400 86400\n")));
  CHECK_STR(send(d, "EV 9 10 20\n"), "ERR FULL");
  CHECK(ok(send(d, "CALGO 600\n")));
  CHECK_STR(send(d, "EV 10 10 20\n"), "ERR NOCAL");  // CALGO closes staging
  CHECK_STR(send(d, "NET\n"), "OK NET W0 -");
  d.set_net(rma3::kNetUp, "192.168.200.123");
  CHECK_STR(send(d, "NET\n"), "OK NET W2 192.168.200.123");
}

void test_wifi_commands_usb_only() {
  Device d;
  for (const char* s : {"WIFI home\n", "PASS secretpass\n", "WIFIGO\n", "WIFICLR\n"}) {
    CHECK_STR(send(d, s, 0, Source::Net), "ERR USBONLY");
  }
  rma3::WifiCreds c;
  CHECK(d.take_wifi_action(&c) == rma3::WifiAction::None);
  CHECK_STR(send(d, "WIFIGO\n"), "ERR BADARG");  // nothing staged
  CHECK(ok(send(d, "WIFI My Home Net\n")));       // spaces kept
  std::string r = send(d, "PASS hunter2hunter2\n");
  CHECK(ok(r) && r.find("hunter2") == std::string::npos);  // never echoed
  CHECK(ok(send(d, "WIFIGO\n")));
  CHECK(d.take_wifi_action(&c) == rma3::WifiAction::Save);
  CHECK_STR(c.ssid, "My Home Net");
  CHECK_STR(c.pass, "hunter2hunter2");
  CHECK(d.take_wifi_action(&c) == rma3::WifiAction::None);  // once
  CHECK(send(d, "NET\n").find("hunter2") == std::string::npos);
  CHECK(send(d, "STATE\n").find("Home") == std::string::npos);
  // Lengths.
  CHECK(ok(send(d, "WIFI " + std::string(32, 'a') + "\n")));
  CHECK_STR(send(d, "WIFI " + std::string(33, 'a') + "\n"), "ERR BADARG");
  CHECK_STR(send(d, "WIFI \n"), "ERR BADARG");
  CHECK_STR(send(d, "PASS 1234567\n"), "ERR BADARG");
  CHECK(ok(send(d, "PASS 12345678\n")));
  CHECK(ok(send(d, "PASS " + std::string(63, 'p') + "\n")));
  CHECK_STR(send(d, "PASS " + std::string(64, 'p') + "\n"), "ERR BADARG");
  CHECK(ok(send(d, "PASS\n")));  // open network
  CHECK(ok(send(d, "WIFICLR\n")));
  CHECK(d.take_wifi_action(&c) == rma3::WifiAction::Clear);
}

// A network client sends half a line; a USB newline must not complete it.
void test_sources_never_mix() {
  Device d;
  CHECK_STR(send(d, "WIFIC", 0, Source::Net), "");
  CHECK_STR(send(d, "\nHELLO\n", 0, Source::Usb), "RMA 3 0.3.0");
  rma3::WifiCreds c;
  CHECK(d.take_wifi_action(&c) == rma3::WifiAction::None);
  CHECK_STR(send(d, "LR\n", 0, Source::Net), "ERR USBONLY");  // finished on its own source
  CHECK(d.take_wifi_action(&c) == rma3::WifiAction::None);
  // Interleaved bytes from both sources each parse on their own.
  char out[Device::kMaxReply];
  std::string a = "LIGHT 0\n", b = "STATE\n";
  std::vector<std::string> usb, net;
  for (size_t i = 0; i < 8; ++i) {
    if (i < a.size() && d.feed(Source::Usb, a[i], 0, out)) usb.push_back(out);
    if (i < b.size() && d.feed(Source::Net, b[i], 0, out)) net.push_back(out);
  }
  CHECK(usb.size() == 1 && net.size() == 1);
  CHECK(!usb.empty() && usb[0].rfind("OK L0", 0) == 0);
}

void test_stages_and_melody_once() {
  Device d;
  send(d, "BAL 255 255 255\n");
  d.tick(0, true);
  CHECK(d.stage() == rma3::kStageNone);
  CHECK(d.led_level(2) > 0 && d.led_level(0) == 0);  // dim blue: no schedule yet
  push(d, 0, 42, 400, 60);  // meeting in 400 s, 60 s long
  d.tick(0, true);
  CHECK(d.stage() == rma3::kStageFree);
  CHECK(d.led_level(1) > 0 && d.led_level(0) == 0);  // green
  CHECK(run(d, 0, 100 * kS) == 0);
  d.tick(100 * kS + 500, true);
  CHECK(d.stage() == rma3::kStageSoon);              // 5 min before
  CHECK(d.led_level(0) > 0 && d.led_level(2) == 0);  // amber
  CHECK(run(d, 100 * kS, 399 * kS) == 0);            // silent while amber
  CHECK(run(d, 399 * kS, 401 * kS) == 1);            // melody at the start
  d.tick(402 * kS, true);
  CHECK(d.stage() == rma3::kStageMeeting);
  uint16_t fresh = d.led_level(0);
  d.tick(420 * kS, true);
  CHECK(d.led_level(0) < fresh && d.led_level(0) > 0);  // dims after 10 s
  // Refreshes with offsets a few seconds off (delivery delay) never replay it.
  for (int skew : {-5, 3, 5}) push(d, 420 * kS, 42, -20 + skew, 60);
  CHECK(run(d, 420 * kS, 470 * kS) == 0);
  CHECK(run(d, 470 * kS, 500 * kS) == 0);
  d.tick(500 * kS, true);
  CHECK(d.stage() == rma3::kStageFree);  // meeting over
}

void test_late_rule() {
  // First seen 30 s after its start (robot rebooted just before): plays.
  Device a;
  push(a, 1000 * kS, 7, -30, 600);
  CHECK(run(a, 1000 * kS, 1001 * kS) == 1);
  // First seen 90 s after its start (app opened mid-meeting): silent.
  Device b;
  push(b, 1000 * kS, 8, -90, 600);
  CHECK(run(b, 1000 * kS, 1100 * kS) == 0);
  b.tick(1100 * kS, true);
  CHECK(b.stage() == rma3::kStageMeeting);
}

void test_back_to_back_and_overlap() {
  Device d;
  CHECK(ok(send(d, "CAL\n", 0)));
  CHECK(ok(send(d, "EV 1 10 70\n", 0)));
  CHECK(ok(send(d, "EV 2 70 130\n", 0)));   // starts as #1 ends
  CHECK(ok(send(d, "EV 3 100 200\n", 0)));  // overlaps #2
  CHECK(ok(send(d, "CALGO 900\n", 0)));
  CHECK(run(d, 0, 210 * kS) == 3);
  d.tick(65 * kS, true);
  CHECK(d.stage() == rma3::kStageMeeting);  // in meeting beats "soon"
  d.tick(150 * kS, true);
  CHECK(d.stage() == rma3::kStageMeeting);  // until the last overlap ends
}

void test_stale_schedule() {
  Device d;
  push(d, 0, 5, 3000, 60);  // valid for 900 s
  d.tick(899 * kS, true);
  CHECK(d.stage() == rma3::kStageFree);
  d.tick(901 * kS, true);
  CHECK(d.stage() == rma3::kStageNone);  // Mac went quiet: dim blue
  CHECK(run(d, 901 * kS, 3100 * kS) == 0);  // and no alerts from an old calendar
}

void test_wraparound() {
  Device d;
  uint32_t now = UINT32_MAX - 5 * kS;
  push(d, now, 9, 10, 60);
  CHECK(run(d, now, now + 9 * kS) == 0);
  CHECK(run(d, now + 9 * kS, now + 11 * kS) == 1);  // across the 32-bit wrap
  d.tick(now + 20 * kS, true);
  CHECK(d.stage() == rma3::kStageMeeting);
}

void test_alert_sound_rules() {
  // Off: silent alert. Manual mode: no alert sound.
  Device off;
  send(off, "SOUND 0\n");
  push(off, 0, 1, 2, 60);
  CHECK(run(off, 0, 5 * kS) == 0);
  Device man;
  send(man, "MODE 0\n");
  push(man, 0, 1, 2, 60);
  CHECK(run(man, 0, 5 * kS) == 0);
  // Volume 0 in volume mode: the melody runs but makes no sound.
  Device vol;
  send(vol, "METER 2\n");
  for (int i = 0; i < 40; ++i) vol.knob_sample(0);
  push(vol, 0, 1, 2, 60);
  run(vol, 0, 2100);
  CHECK(vol.tone().hz == 0);
  // No host connected (on a phone charger): the alert still plays.
  Device alone;
  push(alone, 0, 1, 2, 60);
  CHECK(run(alone, 0, 2500, false) == 1);
  CHECK(alone.playing());
  // A manual preview still stops on disconnect.
  Device prev;
  send(prev, "PLAY 1\n", 0);
  prev.tick(10, false);
  CHECK(!prev.playing());
}

void test_schedule_capacity_of_remembered_ids() {
  rma3::Schedule s;
  // 20 meetings over time; each fires once.
  int fired = 0;
  for (int i = 0; i < 20; ++i) {
    uint32_t base = i * 100 * kS;
    s.begin(base);
    s.add(static_cast<uint16_t>(100 + i), 1, 50);
    s.commit(base, 600);
    for (uint32_t t = base; t < base + 60 * kS; t += 100) fired += s.tick(t);
  }
  CHECK(fired == 20);
}

void test_wifi_record() {
  rma3::WifiCreds c;
  std::strcpy(c.ssid, "Test Net");
  std::strcpy(c.pass, "abcdefgh12");
  uint8_t rec[rma3::kWifiRecordSize];
  rma3::encode_wifi_record(c, rec);
  rma3::WifiCreds back;
  CHECK(rma3::decode_wifi_record(rec, &back));
  CHECK_STR(back.ssid, "Test Net");
  CHECK_STR(back.pass, "abcdefgh12");
  rec[50] ^= 1;  // corrupted
  CHECK(!rma3::decode_wifi_record(rec, &back));
  uint8_t erased[rma3::kWifiRecordSize];
  std::memset(erased, 0xFF, sizeof erased);
  CHECK(!rma3::decode_wifi_record(erased, &back));
  rma3::encode_wifi_record(c, rec);
  rec[0] = 'X';
  CHECK(!rma3::decode_wifi_record(rec, &back));
  CHECK(rma3::crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xCBF43926u);
}

void test_wifi_supervisor() {
  using rma3::WifiLink;
  rma3::WifiSupervisor w;
  CHECK(!w.tick(0, WifiLink::Down) && w.status() == rma3::kNetNone);  // nothing stored
  w.set_have_creds(true, 0);
  CHECK(w.tick(0, WifiLink::Down));            // join at once
  CHECK(!w.tick(5000, WifiLink::Joining));
  CHECK(w.status() == rma3::kNetJoining);
  CHECK(!w.tick(9000, WifiLink::Joining));
  CHECK(w.tick(15010, WifiLink::Failed));      // 10 s later: try again
  CHECK(w.status() == rma3::kNetFailed);
  CHECK(!w.tick(15020, WifiLink::Failed));
  CHECK(!w.tick(30000, WifiLink::Failed));
  CHECK(w.tick(35030, WifiLink::Failed));      // then 20 s
  CHECK(!w.tick(36000, WifiLink::Up) && w.status() == rma3::kNetUp);
  CHECK(w.take_came_up() && !w.take_came_up());
  // Router reboot: link just reads Down. Rejoin after 10 s.
  CHECK(!w.tick(100000, WifiLink::Down));
  CHECK(!w.tick(109000, WifiLink::Down));
  CHECK(w.tick(110000, WifiLink::Down));
  CHECK(!w.tick(111000, WifiLink::Up));
  CHECK(w.take_came_up());                     // re-announce mDNS
  // Back-off caps at 60 s.
  rma3::WifiSupervisor x;
  x.set_have_creds(true, 0);
  uint32_t t = 0;
  std::vector<uint32_t> joins;
  for (; t < 400000; t += 10) {
    if (x.tick(t, WifiLink::Failed)) joins.push_back(t);
  }
  CHECK(joins.size() >= 6);
  for (size_t i = 4; i + 1 < joins.size(); ++i) CHECK(joins[i + 1] - joins[i] <= 60010);
}

void test_pick_meetings() {
  using rma3::CalEvent;
  double now = 1'800'000'000.0;
  auto ev = [&](const char* id, double s, double e) {
    CalEvent c;
    c.id = id;
    c.start = now + s;
    c.end = now + e;
    return c;
  };
  std::vector<CalEvent> in;
  in.push_back(ev("later", 3600, 7200));
  in.push_back(ev("running", -600, 600));
  CalEvent allday = ev("allday", -3600, 80000); allday.all_day = true; in.push_back(allday);
  CalEvent dec = ev("declined", 100, 200); dec.declined = true; in.push_back(dec);
  CalEvent fr = ev("free", 100, 200); fr.free = true; in.push_back(fr);
  CalEvent can = ev("cancelled", 100, 200); can.cancelled = true; in.push_back(can);
  in.push_back(ev("past", -7200, -3600));
  in.push_back(ev("too-far", 13 * 3600, 14 * 3600));
  in.push_back(ev("multiday", -200000, 200000));
  auto out = rma3::pick_meetings(now + 0.4, in);
  CHECK(out.size() == 3);
  if (out.size() == 3) {
    CHECK(out[0].start == -86400 && out[0].end == 86400);  // clamped
    CHECK(out[1].start == -601 && out[1].end == 600);      // running, floor/ceil
    CHECK(out[2].start == 3599);
    CHECK(out[1].id == rma3::meeting_id("running", now - 600));
  }
  // At most 8, running first.
  std::vector<CalEvent> busy;
  for (int i = 0; i < 10; ++i) busy.push_back(ev("m", 600 + i * 60.0, 650 + i * 60.0));
  busy.push_back(ev("now", -60, 60));
  auto b = rma3::pick_meetings(now, busy);
  CHECK(b.size() == 8);
  CHECK(!b.empty() && b[0].start < 0);
  // Ids: stable, non-zero, different for different starts.
  CHECK(rma3::meeting_id("x", now) == rma3::meeting_id("x", now));
  CHECK(rma3::meeting_id("x", now) != rma3::meeting_id("x", now + 3600));
  CHECK(rma3::meeting_id("", 0) != 0);
}

}  // namespace

int main() {
  test_new_commands();
  test_wifi_commands_usb_only();
  test_sources_never_mix();
  test_stages_and_melody_once();
  test_late_rule();
  test_back_to_back_and_overlap();
  test_stale_schedule();
  test_wraparound();
  test_alert_sound_rules();
  test_schedule_capacity_of_remembered_ids();
  test_wifi_record();
  test_wifi_supervisor();
  test_pick_meetings();
  if (g_failures == 0) std::printf("test_v3: ok\n");
  return g_failures;
}

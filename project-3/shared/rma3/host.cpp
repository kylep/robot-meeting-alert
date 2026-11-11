#include "host.hpp"

#include "auth.hpp"
#include "melodies.hpp"

#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef _WIN32
// Windows has no arc4random: the same two calls on the system RNG.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>

namespace {
void arc4random_buf(void* buf, size_t n) {
  if (BCryptGenRandom(nullptr, static_cast<PUCHAR>(buf), static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    std::abort();  // never fall back to predictable nonces or passphrases
  }
}
uint32_t arc4random_uniform(uint32_t n) {
  uint32_t limit = UINT32_MAX - UINT32_MAX % n, v;  // reject the biased tail
  do arc4random_buf(&v, sizeof v); while (v >= limit);
  return v % n;
}
}  // namespace
#endif

namespace rma3 {
namespace {

std::vector<std::string_view> split(std::string_view s) {
  std::vector<std::string_view> out;
  size_t i = 0;
  while (i < s.size()) {
    size_t j = s.find(' ', i);
    if (j == std::string_view::npos) j = s.size();
    out.push_back(s.substr(i, j - i));
    i = j + 1;
  }
  return out;
}

bool dec(std::string_view s, int max, int* v) {
  if (s.empty() || s.size() > 3) return false;
  int x = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    x = x * 10 + (c - '0');
  }
  if (x > max) return false;
  *v = x;
  return true;
}

int hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

// "RRGGBB" (upper-case hex, as the robot writes it).
bool colour(std::string_view s, uint8_t rgb[3]) {
  if (s.size() != 6) return false;
  for (int k = 0; k < 3; ++k) {
    int hi = hex(s[2 * k]), lo = hex(s[2 * k + 1]);
    if (hi < 0 || lo < 0) return false;
    rgb[k] = static_cast<uint8_t>(hi * 16 + lo);
  }
  return true;
}

}  // namespace

bool parse_state(std::string_view line, State* out) {
  auto t = split(line);
  if (t.size() != 10 || t[0] != "OK") return false;
  auto field = [&](size_t i, char tag) -> std::string_view {
    if (t[i].size() < 2 || t[i][0] != tag) return {};
    return t[i].substr(1);
  };
  State s;
  int v = 0;
  if (!dec(field(1, 'L'), 1, &v)) return false;
  s.light = v == 1;
  uint8_t rgb[3];
  if (!colour(field(2, 'C'), rgb)) return false;
  s.r = rgb[0]; s.g = rgb[1]; s.b = rgb[2];
  if (!dec(field(3, 'S'), kSoundCount - 1, &v)) return false;
  s.sound = v;
  if (!dec(field(4, 'M'), 3, &v)) return false;
  s.meter = v;
  if (!dec(field(5, 'K'), 100, &v)) return false;
  s.knob = v;
  if (!dec(field(6, 'Z'), 1, &v)) return false;
  s.playing = v == 1;
  if (!dec(field(7, 'T'), 1, &v)) return false;
  s.mode = v;
  if (!dec(field(8, 'A'), 3, &v)) return false;
  s.stage = v;
  if (!dec(field(9, 'W'), 3, &v)) return false;
  s.net = v;
  *out = s;
  return true;
}

bool parse_config(std::string_view line, Config* out) {
  auto t = split(line);
  if (t.size() != 6 + kLooks || t[0] != "OK" || t[1] != "CFG") return false;
  auto num = [&](size_t i, char tag, int max, int* v) {
    return t[i].size() >= 2 && t[i][0] == tag && dec(t[i].substr(1), max, v);
  };
  Config c;
  int vol = 0, bright = 0, warn = 0, start = 0;
  if (!num(2, 'V', 100, &vol) || !num(3, 'B', 100, &bright) || !num(4, 'W', kMaxWarnMin, &warn) ||
      !num(5, 'S', 1, &start)) {
    return false;
  }
  c.volume = static_cast<uint8_t>(vol);
  c.brightness = static_cast<uint8_t>(bright);
  c.warn_min = static_cast<uint8_t>(warn);
  c.start_sound = start == 1;
  for (int i = 0; i < kLooks; ++i) {
    std::string_view s = t[6 + i];
    LookColour& l = c.look[i];
    uint8_t rgb[3];
    if (s == "-") {
      l.none = true;
    } else if (colour(s, rgb)) {
      l = LookColour{false, rgb[0], rgb[1], rgb[2]};
    } else {
      return false;
    }
  }
  *out = c;
  return true;
}

std::string look_command(int look, const LookColour& c) {
  std::string cmd = "COL " + std::to_string(look);
  if (c.none) return cmd + " -";
  return cmd + " " + std::to_string(c.r) + " " + std::to_string(c.g) + " " + std::to_string(c.b);
}

uint16_t meeting_id(const std::string& event_id, double start) {
  uint32_t h = 2166136261u;  // FNV-1a
  auto mix = [&](unsigned char c) { h = (h ^ c) * 16777619u; };
  for (unsigned char c : event_id) mix(c);
  long long s = static_cast<long long>(std::floor(start));
  for (int i = 0; i < 8; ++i) mix(static_cast<unsigned char>(s >> (8 * i)));
  uint16_t id = static_cast<uint16_t>((h >> 16) ^ (h & 0xFFFF));
  return id == 0 ? 1 : id;
}

std::vector<Pushed> pick_meetings(double now, const std::vector<CalEvent>& events) {
  constexpr double kWindow = 12 * 3600;
  constexpr int32_t kMaxOff = 86400;
  std::vector<const CalEvent*> keep;
  for (const CalEvent& e : events) {
    if (e.all_day || e.declined || e.free || e.cancelled) continue;
    if (e.end <= now || e.start >= now + kWindow || e.end <= e.start) continue;
    keep.push_back(&e);
  }
  // Running meetings first (they decide the light now), then by start.
  std::stable_sort(keep.begin(), keep.end(), [&](const CalEvent* a, const CalEvent* b) {
    bool ra = a->start <= now, rb = b->start <= now;
    if (ra != rb) return ra;
    return a->start < b->start;
  });
  if (keep.size() > 8) keep.resize(8);
  std::vector<Pushed> out;
  for (const CalEvent* e : keep) {
    double s = std::floor(e->start - now), en = std::ceil(e->end - now);
    Pushed p;
    p.id = meeting_id(e->id, e->start);
    p.start = static_cast<int32_t>(std::max<double>(s, -kMaxOff));
    p.end = static_cast<int32_t>(std::min<double>(en, kMaxOff));
    if (p.end <= p.start) p.end = p.start + 1;
    out.push_back(p);
  }
  return out;
}

std::string generate_passphrase() {
  static const char kChars[] = "abcdefghjkmnpqrstuvwxyz23456789";  // no 0/o, 1/l/i
  constexpr uint32_t kN = sizeof kChars - 1;  // 31
  std::string out;
  for (int i = 0; i < 20; ++i) {
    if (i && i % 4 == 0) out += '-';
    out += kChars[arc4random_uniform(kN)];
  }
  return out;
}

std::string NetSession::hello() {
  active_ = false;
  counter_ = 0;
  arc4random_buf(nm_, sizeof nm_);
  char hex[33];
  to_hex(nm_, 16, hex);
  return std::string("HI ") + hex;
}

bool NetSession::answer(const uint8_t key[16], const std::string& reply, std::string* auth_line) {
  // "OK HI " + 32 hex nr + " " + 32 hex proof
  if (reply.size() != 6 + 32 + 1 + 32 || reply.rfind("OK HI ", 0) != 0 || reply[38] != ' ') return false;
  uint8_t proof[16], want[16];
  if (!from_hex(reply.substr(6, 32).c_str(), nr_, 16) || !from_hex(reply.substr(39).c_str(), proof, 16)) {
    return false;
  }
  std::memcpy(key_, key, 16);
  robot_proof(key_, nm_, nr_, want);
  if (!same_bytes(proof, want, 16)) return false;  // not our robot: give it nothing
  uint8_t mine[16];
  mac_proof(key_, nm_, nr_, mine);
  session_key(key_, nm_, nr_, ks_);
  char hex[33];
  to_hex(mine, 16, hex);
  *auth_line = std::string("AUTH ") + hex;
  return true;
}

std::string NetSession::sign(const std::string& line) {
  if (!active_) return line;
  uint8_t tag[kTagLen];
  line_tag(ks_, counter_++, line.data(), line.size(), tag);
  char hex[2 * kTagLen + 1];
  to_hex(tag, kTagLen, hex);
  return line + " #" + hex;
}

bool parse_net(std::string_view line, int* status, std::string* ip) {
  auto t = split(line);
  if (t.size() != 4 || t[0] != "OK" || t[1] != "NET" || t[2].size() != 2 || t[2][0] != 'W') return false;
  int v = 0;
  if (!dec(t[2].substr(1), 3, &v) || t[3].empty() || t[3].size() > 15) return false;
  *status = v;
  *ip = std::string(t[3]);
  return true;
}

bool parse_hello(std::string_view line, std::string* fw_version) {
  constexpr std::string_view kPrefix = "RMA 3 ";
  if (line.size() <= kPrefix.size() || line.substr(0, kPrefix.size()) != kPrefix) return false;
  std::string_view ver = line.substr(kPrefix.size());
  if (ver.find(' ') != std::string_view::npos) return false;
  *fw_version = std::string(ver);
  return true;
}

}  // namespace rma3

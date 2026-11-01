#include "host.hpp"

#include "melodies.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

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
  std::string_view col = field(2, 'C');
  if (col.size() != 6) return false;
  uint8_t rgb[3];
  for (int k = 0; k < 3; ++k) {
    int hi = hex(col[2 * k]), lo = hex(col[2 * k + 1]);
    if (hi < 0 || lo < 0) return false;
    rgb[k] = static_cast<uint8_t>(hi * 16 + lo);
  }
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

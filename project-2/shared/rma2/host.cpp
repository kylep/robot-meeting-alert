#include "host.hpp"

#include <vector>

namespace rma2 {
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
  if (t.size() != 7 || t[0] != "OK") return false;
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
  if (!dec(field(3, 'S'), 4, &v)) return false;
  s.sound = v;
  if (!dec(field(4, 'M'), 3, &v)) return false;
  s.meter = v;
  if (!dec(field(5, 'K'), 100, &v)) return false;
  s.knob = v;
  if (!dec(field(6, 'Z'), 1, &v)) return false;
  s.playing = v == 1;
  *out = s;
  return true;
}

bool parse_hello(std::string_view line, std::string* fw_version) {
  constexpr std::string_view kPrefix = "RMA 2 ";
  if (line.size() <= kPrefix.size() || line.substr(0, kPrefix.size()) != kPrefix) return false;
  std::string_view ver = line.substr(kPrefix.size());
  if (ver.find(' ') != std::string_view::npos) return false;
  *fw_version = std::string(ver);
  return true;
}

}  // namespace rma2

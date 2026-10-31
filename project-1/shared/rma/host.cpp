#include "host.hpp"

namespace rma {
namespace {

bool bit_at(std::string_view s, size_t i, char tag, bool* v) {
  if (s[i] != tag) return false;
  char c = s[i + 1];
  if (c != '0' && c != '1') return false;
  *v = (c == '1');
  return true;
}

}  // namespace

std::string format_state(const Outputs& o) {
  std::string s = "R0 G0 B0 Z0";
  s[1] = o.r ? '1' : '0';
  s[4] = o.g ? '1' : '0';
  s[7] = o.b ? '1' : '0';
  s[10] = o.buzz ? '1' : '0';
  return s;
}

bool parse_state_reply(std::string_view line, Outputs* out) {
  // "OK R1 G0 B0 Z0" is exactly 14 chars.
  if (line.size() != 14 || line.substr(0, 3) != "OK ") return false;
  if (line[5] != ' ' || line[8] != ' ' || line[11] != ' ') return false;
  Outputs o;
  if (!bit_at(line, 3, 'R', &o.r) || !bit_at(line, 6, 'G', &o.g) ||
      !bit_at(line, 9, 'B', &o.b) || !bit_at(line, 12, 'Z', &o.buzz)) {
    return false;
  }
  *out = o;
  return true;
}

bool parse_hello_reply(std::string_view line, std::string* fw_version) {
  constexpr std::string_view kPrefix = "RMA 1 ";
  if (line.size() <= kPrefix.size() || line.substr(0, kPrefix.size()) != kPrefix) {
    return false;
  }
  std::string_view ver = line.substr(kPrefix.size());
  if (ver.find(' ') != std::string_view::npos) return false;
  *fw_version = std::string(ver);
  return true;
}

}  // namespace rma

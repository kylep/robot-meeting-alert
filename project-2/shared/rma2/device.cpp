#include "device.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef RMA_FW_VERSION
#define RMA_FW_VERSION "0.2.0"
#endif

namespace rma2 {
namespace {

void copy_reply(char* out, const char* s) {
  std::strncpy(out, s, Device::kMaxReply - 1);
  out[Device::kMaxReply - 1] = '\0';
}

// Splits line in place on runs of spaces. Returns the token count, or
// kMaxTok + 1 if there are more tokens than fit.
constexpr int kMaxTok = 5;
int tokenize(char* line, char* tok[kMaxTok]) {
  int n = 0;
  char* p = line;
  while (*p != '\0') {
    while (*p == ' ') ++p;
    if (*p == '\0') break;
    if (n == kMaxTok) return kMaxTok + 1;
    tok[n++] = p;
    while (*p != '\0' && *p != ' ') ++p;
    if (*p == ' ') *p++ = '\0';
  }
  return n;
}

// Strict decimal 0..max: digits only, at most 3 of them.
bool parse_uint(const char* s, int max, int* v) {
  size_t len = std::strlen(s);
  if (len == 0 || len > 3) return false;
  int x = 0;
  for (size_t i = 0; i < len; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    x = x * 10 + (s[i] - '0');
  }
  if (x > max) return false;
  *v = x;
  return true;
}

float brightness_factor(const Settings& s, uint8_t knob) {
  if (s.meter == kMeterBright || s.meter == kMeterBoth) return knob / 100.0f;
  return 1.0f;
}

}  // namespace

uint16_t gamma16(uint8_t v) {
  return static_cast<uint16_t>(std::lround(std::pow(v / 255.0, 2.2) * 65535.0));
}

uint16_t volume_permille(uint8_t pct) {
  if (pct == 0) return 0;
  return static_cast<uint16_t>(std::lround(500.0 * std::pow(pct / 100.0, 0.7)));
}

Device::Device() {
  for (int i = 0; i < 3; ++i) bal_[i] = kDefaultBalance[i];
}

bool Device::feed(char c, uint32_t now_ms, char* out) {
  if (c == '\r') return false;
  if (c != '\n') {
    if (len_ < kMaxLine) {
      line_[len_++] = c;
    } else {
      overflow_ = true;
    }
    return false;
  }
  bool was_overflow = overflow_;
  line_[len_] = '\0';
  len_ = 0;
  overflow_ = false;
  if (was_overflow) {
    copy_reply(out, "ERR TOOLONG");
    return true;
  }
  for (char* p = line_; *p; ++p) {
    if (*p == '\t') *p = ' ';
  }
  bool blank = true;
  for (const char* p = line_; *p; ++p) {
    if (*p != ' ') { blank = false; break; }
  }
  if (blank) return false;
  handle_line(now_ms, out);
  return true;
}

void Device::handle_line(uint32_t now_ms, char* reply) {
  char* tok[kMaxTok] = {};
  int n = tokenize(line_, tok);
  const char* verb = tok[0];
  auto is = [&](const char* v) { return std::strcmp(verb, v) == 0; };
  auto bad = [&] { copy_reply(reply, "ERR BADARG"); };
  int a = 0, b = 0, c = 0;

  if (is("HELLO")) {
    if (n != 1) return bad();
    copy_reply(reply, "RMA 2 " RMA_FW_VERSION);
    return;
  }
  if (is("STATE")) {
    if (n != 1) return bad();
  } else if (is("LIGHT")) {
    if (n != 2 || !parse_uint(tok[1], 1, &a)) return bad();
    s_.light = (a == 1);
  } else if (is("RGB") || is("BAL")) {
    if (n != 4 || !parse_uint(tok[1], 255, &a) || !parse_uint(tok[2], 255, &b) ||
        !parse_uint(tok[3], 255, &c)) {
      return bad();
    }
    if (is("RGB")) {
      s_.r = a; s_.g = b; s_.b = c;
    } else {  // BAL: white-balance tuning at acceptance (not persisted)
      bal_[0] = a; bal_[1] = b; bal_[2] = c;
    }
  } else if (is("SOUND")) {
    if (n != 2 || !parse_uint(tok[1], kSoundCount - 1, &a)) return bad();
    s_.sound = a;  // never starts or stops playback
  } else if (is("PLAY")) {
    if (n == 1) {
      seq_.start(s_.sound, now_ms);
    } else if (n == 2 && parse_uint(tok[1], kSoundCount - 1, &a)) {
      seq_.start(a, now_ms);
    } else {
      return bad();
    }
  } else if (is("STOP")) {
    if (n != 1) return bad();
    seq_.stop();
  } else if (is("METER")) {
    if (n != 2 || !parse_uint(tok[1], kMeterBoth, &a)) return bad();
    s_.meter = a;
  } else {
    copy_reply(reply, "ERR BADCMD");
    return;
  }
  write_state(reply);
}

void Device::tick(uint32_t now_ms, bool host_connected) {
  if (!host_connected) seq_.stop();
  seq_.tick(now_ms);
}

uint16_t Device::led_level(int ch) const {
  if (!s_.light || ch < 0 || ch > 2) return 0;
  const uint8_t v[3] = {s_.r, s_.g, s_.b};
  float bright = brightness_factor(s_, knob_.percent());
  float lin = gamma16(v[ch]) / 65535.0f * std::pow(bright, 2.2f) * (bal_[ch] / 255.0f);
  return static_cast<uint16_t>(std::lround(lin * 65535.0f));
}

Tone Device::tone() const {
  Tone t;
  t.hz = seq_.hz();
  if (t.hz == 0) return t;
  uint8_t vol = (s_.meter == kMeterVol || s_.meter == kMeterBoth) ? knob_.percent() : 100;
  t.duty_permille = volume_permille(vol);
  if (t.duty_permille == 0) t.hz = 0;
  return t;
}

void Device::write_state(char* reply) const {
  char buf[kMaxReply];
  std::snprintf(buf, sizeof buf, "OK L%d C%02X%02X%02X S%d M%d K%d Z%d", s_.light ? 1 : 0, s_.r,
                s_.g, s_.b, s_.sound, s_.meter, knob_.percent(), seq_.playing() ? 1 : 0);
  copy_reply(reply, buf);
}

}  // namespace rma2

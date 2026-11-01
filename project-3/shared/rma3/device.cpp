#include "device.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>

#ifndef RMA_FW_VERSION
#define RMA_FW_VERSION "0.3.0"
#endif

namespace rma3 {
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

// Strict decimal 0..max: digits only, at most `digits` of them.
bool parse_uint(const char* s, long max, int* v, size_t digits = 3) {
  size_t len = std::strlen(s);
  if (len == 0 || len > digits) return false;
  long x = 0;
  for (size_t i = 0; i < len; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    x = x * 10 + (s[i] - '0');
  }
  if (x > max) return false;
  *v = static_cast<int>(x);
  return true;
}

// Strict signed decimal within ±max (optional leading '-').
bool parse_int(const char* s, long max, int32_t* v) {
  bool neg = (*s == '-');
  int x = 0;
  if (!parse_uint(neg ? s + 1 : s, max, &x, 6)) return false;
  *v = neg ? -x : x;
  return true;
}

float brightness_factor(const Settings& s, uint8_t knob) {
  if (s.meter == kMeterBright || s.meter == kMeterBoth) return knob / 100.0f;
  return 1.0f;
}

bool starts_with_word(const char* line, const char* word) {
  size_t n = std::strlen(word);
  return std::strncmp(line, word, n) == 0 && (line[n] == '\0' || line[n] == ' ');
}

}  // namespace

uint16_t gamma16(uint8_t v) {
  return static_cast<uint16_t>(std::lround(std::pow(v / 255.0, 2.2) * 65535.0));
}

uint16_t volume_permille(uint8_t pct) {
  if (pct == 0) return 0;
  return static_cast<uint16_t>(std::lround(500.0 * std::pow(pct / 100.0, 0.7)));
}

float breathe(uint32_t now_ms) {
  float phase = static_cast<float>(now_ms % 2000) / 2000.0f;  // 0..1
  float wave = 0.5f - 0.5f * std::cos(phase * 6.2831853f);     // 0..1..0
  return 0.35f + 0.65f * wave;
}

Device::Device() {
  for (int i = 0; i < 3; ++i) bal_[i] = kDefaultBalance[i];
}

bool Device::feed(Source src, char c, uint32_t now_ms, char* out) {
  LineBuf& in = in_[src == Source::Usb ? 0 : 1];
  if (c == '\r') return false;
  if (c != '\n') {
    if (in.len < kMaxLine) {
      in.line[in.len++] = c;
    } else {
      in.overflow = true;
    }
    return false;
  }
  bool was_overflow = in.overflow;
  in.line[in.len] = '\0';
  in.len = 0;
  in.overflow = false;
  if (was_overflow) {
    copy_reply(out, "ERR TOOLONG");
    return true;
  }
  for (char* p = in.line; *p; ++p) {
    if (*p == '\t') *p = ' ';
  }
  bool blank = true;
  for (const char* p = in.line; *p; ++p) {
    if (*p != ' ') { blank = false; break; }
  }
  if (blank) return false;
  handle_line(src, in.line, now_ms, out);
  return true;
}

// WIFI / PASS / WIFIGO / WIFICLR. USB only: nothing on the network can read
// or change the saved Wi-Fi settings. The password is never echoed.
bool Device::handle_wifi(Source src, char* line, char* reply) {
  bool is_wifi = starts_with_word(line, "WIFI");
  bool is_pass = starts_with_word(line, "PASS");
  bool is_go = std::strcmp(line, "WIFIGO") == 0;
  bool is_clr = std::strcmp(line, "WIFICLR") == 0;
  if (!is_wifi && !is_pass && !is_go && !is_clr) return false;
  if (src != Source::Usb) {
    copy_reply(reply, "ERR USBONLY");
    return true;
  }
  if (is_wifi || is_pass) {
    const char* arg = line[4] == ' ' ? line + 5 : line + 4;  // rest of line, verbatim
    size_t n = std::strlen(arg);
    if (is_wifi) {
      if (n < 1 || n > 32) return copy_reply(reply, "ERR BADARG"), true;
      std::memset(staged_.ssid, 0, sizeof staged_.ssid);
      std::memcpy(staged_.ssid, arg, n);
    } else {
      if (n != 0 && (n < 8 || n > 63)) return copy_reply(reply, "ERR BADARG"), true;
      std::memset(staged_.pass, 0, sizeof staged_.pass);
      std::memcpy(staged_.pass, arg, n);
    }
  } else if (is_go) {
    if (!creds_valid(staged_)) return copy_reply(reply, "ERR BADARG"), true;
    wifi_action_ = WifiAction::Save;
  } else {
    staged_ = WifiCreds{};
    wifi_action_ = WifiAction::Clear;
  }
  write_state(reply);
  return true;
}

void Device::handle_line(Source src, char* line, uint32_t now_ms, char* reply) {
  // Over Wi-Fi, nothing but HELLO / HI / AUTH until the handshake is done,
  // and after it every line must carry a valid tag.
  if (src == Source::Net && net_authed_ && !check_tag(line)) {
    copy_reply(reply, "ERR AUTH");
    return;
  }
  bool net_locked = src == Source::Net && !net_authed_;
  if (net_locked && !starts_with_word(line, "HELLO") && !starts_with_word(line, "HI") &&
      !starts_with_word(line, "AUTH")) {
    copy_reply(reply, has_key_ ? "ERR AUTH" : "ERR NOKEY");
    return;
  }
  if (handle_wifi(src, line, reply)) return;

  char* tok[kMaxTok] = {};
  int n = tokenize(line, tok);
  const char* verb = tok[0];
  auto is = [&](const char* v) { return std::strcmp(verb, v) == 0; };

  if (is("HI") || is("AUTH")) {
    if (src == Source::Usb) return copy_reply(reply, "OK AUTH");  // USB needs no proof
    if (!has_key_) return copy_reply(reply, "ERR NOKEY");
    if (net_authed_) return copy_reply(reply, "ERR AUTH");
    if (is("HI")) {  // the robot proves itself first
      if (hi_done_ || n != 2 || !from_hex(tok[1], nm_, kNonceLen)) return copy_reply(reply, "ERR AUTH");
      hi_done_ = true;
      uint8_t proof[kProofLen];
      robot_proof(key_, nm_, nr_, proof);
      char buf[kMaxReply];
      std::strcpy(buf, "OK HI ");
      to_hex(nr_, kNonceLen, buf + 6);
      buf[6 + 2 * kNonceLen] = ' ';
      to_hex(proof, kProofLen, buf + 7 + 2 * kNonceLen);
      return copy_reply(reply, buf);
    }
    uint8_t got[kProofLen], want[kProofLen];
    bool ok = n == 2 && hi_done_ && !auth_tried_ && from_hex(tok[1], got, kProofLen);
    auth_tried_ = true;  // one try per connection, right or wrong
    if (ok) {
      mac_proof(key_, nm_, nr_, want);
      ok = same_bytes(got, want, kProofLen);
    }
    if (ok) {
      session_key(key_, nm_, nr_, ks_);
      counter_ = 0;
    }
    net_authed_ = ok;
    return copy_reply(reply, ok ? "OK AUTH" : "ERR AUTH");
  }
  if (is("KEY")) {  // USB only: pair this Mac
    if (src != Source::Usb) return copy_reply(reply, "ERR USBONLY");
    uint8_t k[kKeyLen];
    if (n != 2 || !from_hex(tok[1], k, kKeyLen)) return copy_reply(reply, "ERR BADARG");
    if (!has_key_ || !same_bytes(k, key_, kKeyLen)) {
      std::memcpy(key_, k, kKeyLen);
      has_key_ = true;
      key_changed_ = true;
      net_authed_ = false;  // anyone on the old key must prove the new one
    }
    write_state(reply);
    return;
  }
  auto bad = [&] { copy_reply(reply, "ERR BADARG"); };
  auto sched_err = [&](Schedule::Result r) {
    if (r == Schedule::kNoCal) copy_reply(reply, "ERR NOCAL");
    else if (r == Schedule::kFull) copy_reply(reply, "ERR FULL");
    else copy_reply(reply, "ERR BADARG");
  };
  int a = 0, b = 0, c = 0;

  if (is("HELLO")) {
    if (n != 1) return bad();
    copy_reply(reply, "RMA 3 " RMA_FW_VERSION);
    return;
  }
  if (is("NET")) {
    if (n != 1) return bad();
    char buf[kMaxReply];
    std::snprintf(buf, sizeof buf, "OK NET W%d %s", net_, ip_);
    copy_reply(reply, buf);
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
    alert_sound_ = false;
  } else if (is("STOP")) {
    if (n != 1) return bad();
    seq_.stop();
    alert_sound_ = false;
  } else if (is("METER")) {
    if (n != 2 || !parse_uint(tok[1], kMeterBoth, &a)) return bad();
    s_.meter = a;
  } else if (is("MODE")) {
    if (n != 2 || !parse_uint(tok[1], kModeCalendar, &a)) return bad();
    s_.mode = a;
  } else if (is("CAL")) {
    if (n != 1) return bad();
    sched_.begin(now_ms);
  } else if (is("EV")) {
    int32_t s = 0, e = 0;
    if (n != 4 || !parse_uint(tok[1], 65535, &a, 5) || !parse_int(tok[2], Schedule::kMaxOffsetS, &s) ||
        !parse_int(tok[3], Schedule::kMaxOffsetS, &e)) {
      return bad();
    }
    Schedule::Result r = sched_.add(static_cast<uint16_t>(a), s, e);
    if (r != Schedule::kOk) return sched_err(r);
  } else if (is("CALGO")) {
    if (n != 2 || !parse_uint(tok[1], 3600, &a, 4)) return bad();
    Schedule::Result r = sched_.commit(now_ms, static_cast<uint32_t>(a));
    if (r != Schedule::kOk) return sched_err(r);
    now_ = now_ms;
  } else {
    copy_reply(reply, "ERR BADCMD");
    return;
  }
  write_state(reply);
}

void Device::tick(uint32_t now_ms, bool host_connected) {
  now_ = now_ms;
  if (sched_.tick(now_ms) && s_.mode == kModeCalendar) {
    seq_.start(s_.sound, now_ms);  // Off: start() stops, so alerts stay silent
    alert_sound_ = seq_.playing();
  }
  if (!host_connected && !alert_sound_) seq_.stop();
  seq_.tick(now_ms);
  if (!seq_.playing()) alert_sound_ = false;
}

void Device::set_net(NetStatus st, const char* ip) {
  net_ = st;
  std::strncpy(ip_, ip, sizeof ip_ - 1);
  ip_[sizeof ip_ - 1] = '\0';
}

void Device::set_key(const uint8_t* key) {
  has_key_ = key != nullptr;
  if (key) std::memcpy(key_, key, kKeyLen);
}

void Device::begin_net_session(const uint8_t nonce[kNonceLen]) {
  in_[1] = LineBuf{};
  std::memcpy(nr_, nonce, kNonceLen);
  hi_done_ = auth_tried_ = net_authed_ = false;
  counter_ = 0;
}

bool Device::check_tag(char* line) {
  char* mark = std::strrchr(line, '#');
  if (!mark || mark == line || mark[-1] != ' ') return false;
  uint8_t got[kTagLen], want[kTagLen];
  if (!from_hex(mark + 1, got, kTagLen)) return false;
  size_t len = static_cast<size_t>(mark - 1 - line);
  line_tag(ks_, counter_, line, len, want);
  if (!same_bytes(got, want, kTagLen)) return false;
  ++counter_;
  mark[-1] = '\0';
  return true;
}

bool Device::take_new_key(uint8_t out[kKeyLen]) {
  if (!key_changed_) return false;
  key_changed_ = false;
  std::memcpy(out, key_, kKeyLen);
  return true;
}

WifiAction Device::take_wifi_action(WifiCreds* creds) {
  WifiAction a = wifi_action_;
  wifi_action_ = WifiAction::None;
  if (a == WifiAction::Save && creds) *creds = staged_;
  return a;
}

// Calendar mode: the stage picks the colour and how bright it is.
void Device::stage_color(float rgb[3], float* level) const {
  auto set = [&](float r, float g, float b, float l) {
    rgb[0] = r; rgb[1] = g; rgb[2] = b; *level = l;
  };
  switch (sched_.stage(now_)) {
    case kStageFree: set(0, 1, 0, 0.40f); break;
    case kStageSoon: set(1, 120 / 255.0f, 0, breathe(now_)); break;
    case kStageMeeting: set(1, 0, 0, sched_.fresh_start(now_) ? 1.0f : 0.25f); break;
    default: set(0, 0, 1, 0.15f); break;  // no schedule (yet, or stale)
  }
}

uint16_t Device::led_level(int ch) const {
  if (!s_.light || ch < 0 || ch > 2) return 0;
  float colour = 0, level = 1;
  if (s_.mode == kModeCalendar) {
    float rgb[3];
    stage_color(rgb, &level);
    colour = std::pow(rgb[ch], 2.2f);
  } else {
    const uint8_t v[3] = {s_.r, s_.g, s_.b};
    colour = gamma16(v[ch]) / 65535.0f;
  }
  float bright = brightness_factor(s_, knob_.percent()) * level;
  float lin = colour * std::pow(bright, 2.2f) * (bal_[ch] / 255.0f);
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
  std::snprintf(buf, sizeof buf, "OK L%d C%02X%02X%02X S%d M%d K%d Z%d T%d A%d W%d", s_.light ? 1 : 0,
                s_.r, s_.g, s_.b, s_.sound, s_.meter, knob_.percent(), seq_.playing() ? 1 : 0, s_.mode,
                sched_.stage(now_), net_);
  copy_reply(reply, buf);
}

}  // namespace rma3

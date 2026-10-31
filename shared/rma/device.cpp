#include "device.hpp"

#include <cstring>

#ifndef RMA_FW_VERSION
#define RMA_FW_VERSION "0.1.0"
#endif

namespace rma {
namespace {

void copy_reply(char* out, const char* s) {
  std::strncpy(out, s, Device::kMaxReply - 1);
  out[Device::kMaxReply - 1] = '\0';
}

// Splits line in place on single spaces; returns token count (max 4).
int tokenize(char* line, char* tok[4]) {
  int n = 0;
  char* p = line;
  while (*p != '\0') {
    while (*p == ' ') ++p;
    if (*p == '\0') break;
    if (n == 4) return 5;  // too many tokens
    tok[n++] = p;
    while (*p != '\0' && *p != ' ') ++p;
    if (*p == ' ') *p++ = '\0';
  }
  return n;
}

bool parse_bit(const char* s, bool* v) {
  if (std::strcmp(s, "0") == 0) { *v = false; return true; }
  if (std::strcmp(s, "1") == 0) { *v = true; return true; }
  return false;
}

bool expired(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

}  // namespace

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
  // End of line.
  bool was_overflow = overflow_;
  line_[len_] = '\0';
  len_ = 0;
  overflow_ = false;
  if (was_overflow) {
    copy_reply(out, "ERR TOOLONG");
    return true;
  }
  // Empty or whitespace-only lines get no reply.
  bool blank = true;
  for (const char* p = line_; *p; ++p) {
    if (*p != ' ' && *p != '\t') { blank = false; break; }
  }
  if (blank) return false;
  handle_line(now_ms, out);
  return true;
}

void Device::handle_line(uint32_t now_ms, char* reply) {
  for (char* p = line_; *p; ++p) {
    if (*p == '\t') *p = ' ';
  }
  char* tok[4] = {};
  int n = tokenize(line_, tok);
  if (n == 0) return;
  const char* verb = tok[0];

  if (std::strcmp(verb, "HELLO") == 0) {
    if (n != 1) { copy_reply(reply, "ERR BADARG"); return; }
    copy_reply(reply, "RMA 1 " RMA_FW_VERSION);
    return;
  }
  if (std::strcmp(verb, "STATE") == 0) {
    if (n != 1) { copy_reply(reply, "ERR BADARG"); return; }
    write_state(reply);
    return;
  }
  if (std::strcmp(verb, "ALLOFF") == 0) {
    if (n != 1) { copy_reply(reply, "ERR BADARG"); return; }
    out_ = Outputs{};
    write_state(reply);
    return;
  }
  if (std::strcmp(verb, "LED") == 0) {
    bool v = false;
    if (n != 3 || std::strlen(tok[1]) != 1 || !parse_bit(tok[2], &v)) {
      copy_reply(reply, "ERR BADARG");
      return;
    }
    switch (tok[1][0]) {
      case 'R': out_.r = v; break;
      case 'G': out_.g = v; break;
      case 'B': out_.b = v; break;
      default: copy_reply(reply, "ERR BADARG"); return;
    }
    write_state(reply);
    return;
  }
  if (std::strcmp(verb, "BUZZ") == 0) {
    bool v = false;
    if (n != 2 || !parse_bit(tok[1], &v)) {
      copy_reply(reply, "ERR BADARG");
      return;
    }
    out_.buzz = v;
    if (v) buzz_deadline_ = now_ms + kBuzzLeaseMs;
    write_state(reply);
    return;
  }
  copy_reply(reply, "ERR BADCMD");
}

void Device::tick(uint32_t now_ms, bool host_connected) {
  if (!out_.buzz) return;
  if (!host_connected || expired(now_ms, buzz_deadline_)) {
    out_.buzz = false;
  }
}

void Device::write_state(char* reply) const {
  char s[] = "OK R0 G0 B0 Z0";
  s[4] = out_.r ? '1' : '0';
  s[7] = out_.g ? '1' : '0';
  s[10] = out_.b ? '1' : '0';
  s[13] = out_.buzz ? '1' : '0';
  copy_reply(reply, s);
}

}  // namespace rma

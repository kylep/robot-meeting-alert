#include "wifi.hpp"

#include <cstring>

namespace rma3 {
namespace {

constexpr uint8_t kMagic[4] = {'R', 'M', 'A', '3'};
constexpr uint8_t kVersion = 1;
constexpr size_t kSsidOff = 8, kPassOff = 40, kCrcOff = 104;

}  // namespace

bool creds_valid(const WifiCreds& c) {
  size_t s = strnlen(c.ssid, sizeof c.ssid);
  size_t p = strnlen(c.pass, sizeof c.pass);
  return s >= 1 && s <= 32 && (p == 0 || (p >= 8 && p <= 63));
}

uint32_t crc32(const uint8_t* data, size_t len) {
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

void encode_wifi_record(const WifiCreds& c, uint8_t out[kWifiRecordSize]) {
  std::memset(out, 0xFF, kWifiRecordSize);
  std::memcpy(out, kMagic, 4);
  size_t s = strnlen(c.ssid, 32), p = strnlen(c.pass, 63);
  out[4] = kVersion;
  out[5] = static_cast<uint8_t>(s);
  out[6] = static_cast<uint8_t>(p);
  out[7] = 0;
  std::memset(out + kSsidOff, 0, 32 + 64);
  std::memcpy(out + kSsidOff, c.ssid, s);
  std::memcpy(out + kPassOff, c.pass, p);
  uint32_t crc = crc32(out, kCrcOff);
  for (int i = 0; i < 4; ++i) out[kCrcOff + i] = static_cast<uint8_t>(crc >> (8 * i));
}

bool decode_wifi_record(const uint8_t in[kWifiRecordSize], WifiCreds* out) {
  if (std::memcmp(in, kMagic, 4) != 0 || in[4] != kVersion) return false;
  uint32_t crc = 0;
  for (int i = 0; i < 4; ++i) crc |= static_cast<uint32_t>(in[kCrcOff + i]) << (8 * i);
  if (crc != crc32(in, kCrcOff)) return false;
  size_t s = in[5], p = in[6];
  if (s < 1 || s > 32 || p > 63) return false;
  WifiCreds c;
  std::memcpy(c.ssid, in + kSsidOff, s);
  std::memcpy(c.pass, in + kPassOff, p);
  if (!creds_valid(c)) return false;
  *out = c;
  return true;
}

void WifiSupervisor::set_have_creds(bool have, uint32_t now_ms) {
  have_ = have;
  up_ = false;
  backoff_ = kFirstMs;
  pending_ = have;
  next_ = now_ms;  // join straight away
  status_ = have ? kNetJoining : kNetNone;
}

bool WifiSupervisor::tick(uint32_t now_ms, WifiLink link) {
  if (!have_) {
    status_ = kNetNone;
    return false;
  }
  if (link == WifiLink::Up) {
    if (!up_) came_up_ = true;
    up_ = true;
    pending_ = false;
    backoff_ = kFirstMs;
    status_ = kNetUp;
    return false;
  }
  if (up_) {  // just dropped (router reboot, kicked): try again in 10 s
    up_ = false;
    backoff_ = kFirstMs;
    pending_ = true;
    next_ = now_ms + kFirstMs;
  }
  status_ = link == WifiLink::Failed ? kNetFailed : kNetJoining;
  if (!pending_) {  // a join is running: give it until the back-off expires
    pending_ = true;
    next_ = now_ms + backoff_;
    backoff_ = backoff_ * 2 > kMaxMs ? kMaxMs : backoff_ * 2;
    return false;
  }
  if (static_cast<int32_t>(now_ms - next_) >= 0) {
    pending_ = false;
    return true;
  }
  return false;
}

bool WifiSupervisor::take_came_up() {
  bool v = came_up_;
  came_up_ = false;
  return v;
}

}  // namespace rma3

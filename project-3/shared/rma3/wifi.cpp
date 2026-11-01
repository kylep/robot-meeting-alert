#include "wifi.hpp"

#include <cstring>

namespace rma3 {
namespace {

constexpr uint8_t kMagic[4] = {'R', 'M', 'A', '3'};
constexpr uint8_t kVersion = 2;
constexpr size_t kSsidOff = 8, kPassOff = 40, kKeyOff = 104, kCrcOff = 120;

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

void encode_record(const StoredRecord& r, uint8_t out[kWifiRecordSize]) {
  std::memset(out, 0xFF, kWifiRecordSize);
  std::memcpy(out, kMagic, 4);
  bool creds = r.has_creds && creds_valid(r.creds);
  size_t s = creds ? strnlen(r.creds.ssid, 32) : 0, p = creds ? strnlen(r.creds.pass, 63) : 0;
  out[4] = kVersion;
  out[5] = static_cast<uint8_t>(s);
  out[6] = static_cast<uint8_t>(p);
  out[7] = static_cast<uint8_t>((creds ? 1 : 0) | (r.has_key ? 2 : 0));
  std::memset(out + kSsidOff, 0, kCrcOff - kSsidOff);
  std::memcpy(out + kSsidOff, r.creds.ssid, s);
  std::memcpy(out + kPassOff, r.creds.pass, p);
  if (r.has_key) std::memcpy(out + kKeyOff, r.key, 16);
  uint32_t crc = crc32(out, kCrcOff);
  for (int i = 0; i < 4; ++i) out[kCrcOff + i] = static_cast<uint8_t>(crc >> (8 * i));
}

StoredRecord decode_record(const uint8_t in[kWifiRecordSize]) {
  StoredRecord r;
  if (std::memcmp(in, kMagic, 4) != 0 || (in[4] != 1 && in[4] != kVersion)) return r;
  size_t crc_off = in[4] == 1 ? kKeyOff : kCrcOff;  // v1 had no key
  uint32_t crc = 0;
  for (int i = 0; i < 4; ++i) crc |= static_cast<uint32_t>(in[crc_off + i]) << (8 * i);
  if (crc != crc32(in, crc_off)) return r;
  uint8_t flags = in[4] == 1 ? 1 : in[7];
  size_t s = in[5], p = in[6];
  if ((flags & 1) && s >= 1 && s <= 32 && p <= 63) {
    std::memcpy(r.creds.ssid, in + kSsidOff, s);
    std::memcpy(r.creds.pass, in + kPassOff, p);
    r.has_creds = creds_valid(r.creds);
    if (!r.has_creds) r.creds = WifiCreds{};
  }
  if (flags & 2) {
    r.has_key = true;
    std::memcpy(r.key, in + kKeyOff, 16);
  }
  return r;
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

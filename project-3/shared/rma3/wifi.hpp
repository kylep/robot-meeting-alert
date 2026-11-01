// Wi-Fi helpers with no SDK dependency: the saved-settings record kept in
// the Pico's flash, and the supervisor that decides when to (re)join.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rma3 {

enum NetStatus : uint8_t { kNetNone = 0, kNetJoining = 1, kNetUp = 2, kNetFailed = 3 };

struct WifiCreds {
  char ssid[33] = {};  // 1..32 bytes, NUL-terminated
  char pass[64] = {};  // 0 (open network) or 8..63 bytes
};

bool creds_valid(const WifiCreds& c);

// Flash record: one 256-byte page (the flash programming unit).
// Layout: "RMA3" magic, version, ssid_len, pass_len, pad, ssid[32], pass[64],
// CRC32 of everything before it. Erased flash (all 0xFF) reads as "none".
constexpr size_t kWifiRecordSize = 256;
void encode_wifi_record(const WifiCreds& c, uint8_t out[kWifiRecordSize]);
bool decode_wifi_record(const uint8_t in[kWifiRecordSize], WifiCreds* out);
uint32_t crc32(const uint8_t* data, size_t len);

// What the driver reports, boiled down.
enum class WifiLink : uint8_t { Down, Joining, Up, Failed };

// Decides when to call the driver's join. The driver never rejoins on its
// own after a router drops it (the link just reads as Down), so anything
// other than Up for long enough starts a fresh join, with back-off.
class WifiSupervisor {
 public:
  static constexpr uint32_t kFirstMs = 10000;
  static constexpr uint32_t kMaxMs = 60000;

  void set_have_creds(bool have, uint32_t now_ms);
  // Call every loop. Returns true when the caller should start a join now.
  bool tick(uint32_t now_ms, WifiLink link);
  NetStatus status() const { return status_; }
  // True once after each transition to Up (re-announce mDNS then).
  bool take_came_up();

 private:
  bool have_ = false;
  bool up_ = false;
  bool came_up_ = false;
  bool pending_ = false;  // join requested, not yet due
  uint32_t next_ = 0;
  uint32_t backoff_ = kFirstMs;
  NetStatus status_ = kNetNone;
};

}  // namespace rma3

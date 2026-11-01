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

// What the robot keeps in flash: Wi-Fi settings and the pairing key.
struct StoredRecord {
  bool has_creds = false;
  WifiCreds creds;
  bool has_key = false;
  uint8_t key[16] = {};
};

// Flash record: one 256-byte page (the flash programming unit).
// v2 layout: "RMA3" magic, version 2, ssid_len, pass_len, flags (bit 0 Wi-Fi,
// bit 1 key), ssid[32] @8, pass[64] @40, key[16] @104, CRC32 @120 of all
// before it. v1 (no key, CRC @104) is still read. Erased flash reads as empty.
constexpr size_t kWifiRecordSize = 256;
void encode_record(const StoredRecord& r, uint8_t out[kWifiRecordSize]);
StoredRecord decode_record(const uint8_t in[kWifiRecordSize]);
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

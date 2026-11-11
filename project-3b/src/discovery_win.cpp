// discovery.hpp (../project-3/mac/src) on Windows.
//   - USB: COM ports whose hardware ID has the Raspberry Pi vendor ID
//     (USB\VID_2E8A...), found through SetupAPI. Other serial devices are
//     never opened.
//   - Wi-Fi: "rma.local". Windows 10/11 can resolve .local names itself, but
//     not reliably on every setup, so when getaddrinfo fails this asks once
//     with a tiny mDNS query of its own (RFC 6762 "one-shot" query from a
//     normal UDP port; the robot's lwIP answers those directly).
//   - The address typed in Settings is always tried first.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>

#include "discovery.hpp"

namespace rma {
namespace {

bool winsock_ready() {
  static const bool ok = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return ok;
}

std::string narrow(const wchar_t* w) {
  std::string out;
  for (; *w; ++w) out += static_cast<char>(*w < 128 ? *w : '?');
  return out;
}

int com_number(const std::string& name) { return name.size() > 3 ? std::atoi(name.c_str() + 3) : 0; }

}  // namespace

std::vector<std::string> find_pico_ports() {
  std::vector<std::string> ports;
  HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
  if (set == INVALID_HANDLE_VALUE) return ports;
  SP_DEVINFO_DATA info{};
  info.cbSize = sizeof info;
  for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
    wchar_t ids[2048] = {};  // REG_MULTI_SZ: several IDs, each 0-terminated
    if (!SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_HARDWAREID, nullptr, reinterpret_cast<PBYTE>(ids),
                                           sizeof ids - 2 * sizeof(wchar_t), nullptr)) {
      continue;
    }
    bool pico = false;
    for (const wchar_t* id = ids; *id && !pico; id += wcslen(id) + 1) {
      std::wstring up(id);
      for (wchar_t& c : up) c = static_cast<wchar_t>(std::towupper(c));
      char want[16];
      std::snprintf(want, sizeof want, "VID_%04X", kRaspberryPiVendorId);
      pico = narrow(up.c_str()).find(want) != std::string::npos;
    }
    if (!pico) continue;
    HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
    if (key == INVALID_HANDLE_VALUE) continue;
    wchar_t name[64] = {};
    DWORD size = sizeof name - sizeof(wchar_t), type = 0;
    if (RegQueryValueExW(key, L"PortName", nullptr, &type, reinterpret_cast<LPBYTE>(name), &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
      std::string port = narrow(name);
      if (port.rfind("COM", 0) == 0) ports.push_back(port);
    }
    RegCloseKey(key);
  }
  SetupDiDestroyDeviceInfoList(set);
  std::sort(ports.begin(), ports.end(),
            [](const std::string& a, const std::string& b) { return com_number(a) < com_number(b); });
  return ports;
}

namespace {

// Reads a (possibly compressed) DNS name at *pos; false if malformed.
bool read_name(const unsigned char* p, int len, int* pos, std::string* out) {
  out->clear();
  int i = *pos, jumps = 0;
  bool jumped = false;
  while (i < len) {
    int n = p[i];
    if (n == 0) {
      if (!jumped) *pos = i + 1;
      return true;
    }
    if ((n & 0xC0) == 0xC0) {
      if (i + 1 >= len || ++jumps > 16) return false;
      if (!jumped) *pos = i + 2;
      jumped = true;
      i = ((n & 0x3F) << 8) | p[i + 1];
      continue;
    }
    if (i + 1 + n > len) return false;
    if (!out->empty()) *out += '.';
    for (int k = 0; k < n; ++k) *out += static_cast<char>(std::tolower(p[i + 1 + k]));
    i += 1 + n;
  }
  return false;
}

// One mDNS question for `name`'s IPv4 address, answered within `ms`.
bool mdns_lookup(const std::string& name, std::string* ip, int ms) {
  unsigned char q[300] = {};
  int n = 12;
  q[0] = 0x52;  // ID (echoed in one-shot answers)
  q[1] = 0x4D;
  q[5] = 1;  // one question
  size_t start = 0;
  while (start <= name.size()) {
    size_t dot = name.find('.', start);
    if (dot == std::string::npos) dot = name.size();
    size_t l = dot - start;
    if (l == 0 || l > 63 || n + 1 + static_cast<int>(l) > 280) return false;
    q[n++] = static_cast<unsigned char>(l);
    std::memcpy(q + n, name.data() + start, l);
    n += static_cast<int>(l);
    start = dot + 1;
  }
  q[n++] = 0;
  q[n++] = 0;
  q[n++] = 1;  // type A
  q[n++] = 0;
  q[n++] = 1;  // class IN

  SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (s == INVALID_SOCKET) return false;
  DWORD ttl = 255;
  setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl), sizeof ttl);
  sockaddr_in to{};
  to.sin_family = AF_INET;
  to.sin_port = htons(5353);
  inet_pton(AF_INET, "224.0.0.251", &to.sin_addr);
  bool found = false;
  if (sendto(s, reinterpret_cast<const char*>(q), n, 0, reinterpret_cast<sockaddr*>(&to), sizeof to) == n) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!found) {
      auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
      if (left <= 0) break;
      fd_set rd;
      FD_ZERO(&rd);
      FD_SET(s, &rd);
      timeval tv{static_cast<long>(left / 1000), static_cast<long>((left % 1000) * 1000)};
      if (select(0, &rd, nullptr, nullptr, &tv) != 1) break;
      unsigned char r[1500];
      int len = recvfrom(s, reinterpret_cast<char*>(r), sizeof r, 0, nullptr, nullptr);
      if (len < 12 || !(r[2] & 0x80)) continue;  // not a response
      int qd = (r[4] << 8) | r[5], an = (r[6] << 8) | r[7], ar = (r[10] << 8) | r[11];
      int pos = 12;
      std::string rname;
      bool ok = true;
      for (int k = 0; k < qd && ok; ++k) {
        ok = read_name(r, len, &pos, &rname) && pos + 4 <= len;
        pos += 4;
      }
      // Answers, then (lwIP may put the address there) additional records.
      int nscount = (r[8] << 8) | r[9];
      for (int k = 0; k < an + nscount + ar && ok && !found; ++k) {
        ok = read_name(r, len, &pos, &rname) && pos + 10 <= len;
        if (!ok) break;
        int type = (r[pos] << 8) | r[pos + 1];
        int rdlen = (r[pos + 8] << 8) | r[pos + 9];
        pos += 10;
        if (pos + rdlen > len) break;
        if (type == 1 && rdlen == 4 && rname == name) {
          char buf[INET_ADDRSTRLEN] = {};
          if (inet_ntop(AF_INET, r + pos, buf, sizeof buf)) {
            *ip = buf;
            found = true;
          }
        }
        pos += rdlen;
      }
    }
  }
  closesocket(s);
  return found;
}

}  // namespace

RobotFinder::RobotFinder() : worker_([this] { run(); }) {}

RobotFinder::~RobotFinder() {
  stop_ = true;
  if (worker_.joinable()) worker_.join();
}

void RobotFinder::set_manual(const std::string& ip) {
  std::lock_guard<std::mutex> lock(mu_);
  manual_ = ip;
}

std::string RobotFinder::found() const {
  std::lock_guard<std::mutex> lock(mu_);
  return found_;
}

std::vector<std::string> RobotFinder::candidates() const {
  std::vector<std::string> out = find_pico_ports();
  std::lock_guard<std::mutex> lock(mu_);
  for (const std::string& ip : {manual_, found_}) {
    if (ip.empty()) continue;
    std::string c = "tcp:" + ip + ":" + std::to_string(kRobotPort);
    bool dup = false;
    for (const auto& o : out) dup = dup || o == c;
    if (!dup) out.push_back(c);
  }
  return out;
}

void RobotFinder::run() {
  using namespace std::chrono_literals;
  if (!winsock_ready()) return;
  while (!stop_) {
    std::string ip;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo("rma.local", nullptr, &hints, &res) == 0 && res) {
      char buf[INET_ADDRSTRLEN] = {};
      auto* sin = reinterpret_cast<sockaddr_in*>(res->ai_addr);
      if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf)) ip = buf;
      freeaddrinfo(res);
    }
    if (ip.empty()) mdns_lookup("rma.local", &ip, 1500);
    if (!ip.empty()) {
      std::lock_guard<std::mutex> lock(mu_);
      found_ = ip;  // keep the last good one if a later lookup fails
    }
    for (int i = 0; i < 50 && !stop_; ++i) std::this_thread::sleep_for(100ms);
  }
}

}  // namespace rma

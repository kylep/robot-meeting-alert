// SerialPort (../project-3/mac/src/serial_port.hpp) on Windows: the Pico's
// USB serial as a COM port, and Winsock TCP for Wi-Fi.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <string>

#include "serial_port.hpp"

namespace rma {
namespace {

DWORD ms_until(Clock::time_point deadline) {
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
  return ms < 1 ? 1 : static_cast<DWORD>(ms);
}

bool winsock_ready() {
  static const bool ok = [] {
    WSADATA data;
    return WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }();
  return ok;
}

// Reads return as soon as any byte is there, or after read_ms with none.
bool set_timeouts(HANDLE h, DWORD read_ms, DWORD write_ms) {
  COMMTIMEOUTS t{};
  t.ReadIntervalTimeout = MAXDWORD;
  t.ReadTotalTimeoutMultiplier = MAXDWORD;
  t.ReadTotalTimeoutConstant = read_ms;
  t.WriteTotalTimeoutMultiplier = 0;
  t.WriteTotalTimeoutConstant = write_ms;
  return SetCommTimeouts(h, &t) != 0;
}

// Waits until the socket is readable (or writable); 1 ready, 0 timeout, -1 error.
int wait_socket(SOCKET s, bool for_write, Clock::time_point deadline) {
  fd_set set;
  FD_ZERO(&set);
  FD_SET(s, &set);
  fd_set err;
  FD_ZERO(&err);
  FD_SET(s, &err);
  DWORD ms = ms_until(deadline);
  timeval tv{static_cast<long>(ms / 1000), static_cast<long>((ms % 1000) * 1000)};
  int r = select(0, for_write ? nullptr : &set, for_write ? &set : nullptr, &err, &tv);
  if (r == SOCKET_ERROR) return -1;
  if (r == 0) return 0;
  return FD_ISSET(s, &err) ? -1 : 1;
}

}  // namespace

OpenResult SerialPort::open(const std::string& path) {
  close();
  // "\\.\COM12": the prefix is required for COM10 and up, harmless below.
  std::wstring name = L"\\\\.\\";
  for (char c : path) name += static_cast<wchar_t>(static_cast<unsigned char>(c));
  // Share mode 0: exclusive, like TIOCEXCL on the Mac.
  HANDLE h = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) {
    DWORD e = GetLastError();
    if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return OpenResult::NotFound;
    if (e == ERROR_ACCESS_DENIED || e == ERROR_SHARING_VIOLATION) return OpenResult::Busy;
    return OpenResult::Error;
  }
  DCB dcb{};
  dcb.DCBlength = sizeof dcb;
  if (!GetCommState(h, &dcb)) {
    CloseHandle(h);
    return OpenResult::Error;
  }
  // Never 1200: the Pico SDK treats a 1200-baud setting as "reboot to BOOTSEL".
  dcb.BaudRate = CBR_115200;
  dcb.ByteSize = 8;
  dcb.Parity = NOPARITY;
  dcb.StopBits = ONESTOPBIT;
  dcb.fBinary = TRUE;
  dcb.fParity = FALSE;
  dcb.fOutxCtsFlow = FALSE;
  dcb.fOutxDsrFlow = FALSE;
  dcb.fDsrSensitivity = FALSE;
  dcb.fOutX = FALSE;
  dcb.fInX = FALSE;
  dcb.fNull = FALSE;
  dcb.fAbortOnError = FALSE;
  dcb.fDtrControl = DTR_CONTROL_ENABLE;  // "terminal connected", as opening a tty does on the Mac
  dcb.fRtsControl = RTS_CONTROL_ENABLE;
  if (!SetCommState(h, &dcb) || !set_timeouts(h, 1, 1)) {
    CloseHandle(h);
    return OpenResult::Error;
  }
  PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
  com_ = h;
  buf_.clear();
  return OpenResult::Ok;
}

OpenResult SerialPort::open_tcp(const std::string& ip, int port, Clock::duration timeout) {
  close();
  if (!winsock_ready()) return OpenResult::Error;
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(port));
  if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) return OpenResult::NotFound;
  SOCKET s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return OpenResult::Error;
  BOOL one = TRUE;
  setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
  u_long nonblocking = 1;
  ioctlsocket(s, FIONBIO, &nonblocking);
  if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
    if (WSAGetLastError() != WSAEWOULDBLOCK) {
      closesocket(s);
      return OpenResult::NotFound;
    }
    int err = 0;
    int len = sizeof err;
    if (wait_socket(s, true, Clock::now() + timeout) != 1 ||
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&err), &len) != 0 || err != 0) {
      closesocket(s);
      return OpenResult::NotFound;
    }
  }
  sock_ = static_cast<uintptr_t>(s);
  buf_.clear();
  return OpenResult::Ok;
}

void SerialPort::close() {
  if (com_ != nullptr) {
    CloseHandle(static_cast<HANDLE>(com_));
    com_ = nullptr;
  }
  if (sock_ != kNoSocket) {
    closesocket(static_cast<SOCKET>(sock_));
    sock_ = kNoSocket;
  }
  buf_.clear();
}

IoResult SerialPort::write_all(std::string_view data, Clock::time_point deadline) {
  size_t off = 0;
  if (com_ != nullptr) {
    HANDLE h = static_cast<HANDLE>(com_);
    while (off < data.size()) {
      if (!set_timeouts(h, 1, ms_until(deadline))) return IoResult::Dead;
      DWORD n = 0;
      if (!WriteFile(h, data.data() + off, static_cast<DWORD>(data.size() - off), &n, nullptr)) return IoResult::Dead;
      off += n;
      if (off < data.size() && Clock::now() >= deadline) return IoResult::Timeout;
    }
    return IoResult::Ok;
  }
  if (sock_ == kNoSocket) return IoResult::Dead;
  SOCKET s = static_cast<SOCKET>(sock_);
  while (off < data.size()) {
    int n = ::send(s, data.data() + off, static_cast<int>(data.size() - off), 0);
    if (n > 0) {
      off += static_cast<size_t>(n);
      continue;
    }
    if (n == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) return IoResult::Dead;
    int r = wait_socket(s, true, deadline);
    if (r == 0) return IoResult::Timeout;
    if (r < 0) return IoResult::Dead;
  }
  return IoResult::Ok;
}

IoResult SerialPort::read_line(std::string* line, Clock::time_point deadline) {
  if (com_ == nullptr && sock_ == kNoSocket) return IoResult::Dead;
  for (;;) {
    size_t nl = buf_.find('\n');
    if (nl != std::string::npos) {
      std::string out = buf_.substr(0, nl);
      buf_.erase(0, nl + 1);
      while (!out.empty() && out.back() == '\r') out.pop_back();
      *line = std::move(out);
      return IoResult::Ok;
    }
    char tmp[256];
    if (com_ != nullptr) {
      HANDLE h = static_cast<HANDLE>(com_);
      if (!set_timeouts(h, ms_until(deadline), 1)) return IoResult::Dead;
      DWORD n = 0;
      // Fails once the robot is unplugged (the COM port disappears).
      if (!ReadFile(h, tmp, sizeof tmp, &n, nullptr)) return IoResult::Dead;
      if (n > 0) {
        buf_.append(tmp, n);
        continue;
      }
      if (Clock::now() >= deadline) return IoResult::Timeout;
      continue;
    }
    SOCKET s = static_cast<SOCKET>(sock_);
    int r = wait_socket(s, false, deadline);
    if (r == 0) return IoResult::Timeout;
    if (r < 0) return IoResult::Dead;
    int n = ::recv(s, tmp, sizeof tmp, 0);
    if (n > 0) {
      buf_.append(tmp, static_cast<size_t>(n));
      continue;
    }
    if (n == 0) return IoResult::Dead;  // the robot closed the connection
    if (WSAGetLastError() == WSAEWOULDBLOCK) continue;
    return IoResult::Dead;
  }
}

}  // namespace rma

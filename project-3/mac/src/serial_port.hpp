// Thin POSIX wrapper for a line connection to the robot: a USB CDC serial
// port (or a pty in tests), or a TCP socket over Wi-Fi. The Windows build
// (../project-3b/src/serial_port_win.cpp) implements the same class on COM
// ports and Winsock.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace rma {

using Clock = std::chrono::steady_clock;

enum class OpenResult { Ok, NotFound, Busy, Error };
enum class IoResult { Ok, Timeout, Dead };

class SerialPort {
 public:
  SerialPort() = default;
  ~SerialPort() { close(); }
  SerialPort(const SerialPort&) = delete;
  SerialPort& operator=(const SerialPort&) = delete;

  OpenResult open(const std::string& path);  // Windows: "COM5"
  // TCP to a numeric IPv4 address; gives up after `timeout`.
  OpenResult open_tcp(const std::string& ip, int port, Clock::duration timeout);
  void close();
#ifdef _WIN32
  bool is_open() const { return com_ != nullptr || sock_ != kNoSocket; }
#else
  bool is_open() const { return fd_ >= 0; }
#endif

  // Writes everything (handling short writes and EAGAIN) before the deadline.
  IoResult write_all(std::string_view data, Clock::time_point deadline);

  // Reads one line, without the trailing "\n" or "\r".
  IoResult read_line(std::string* line, Clock::time_point deadline);

 private:
#ifdef _WIN32
  static constexpr uintptr_t kNoSocket = ~uintptr_t{0};
  void* com_ = nullptr;          // HANDLE of the COM port
  uintptr_t sock_ = kNoSocket;   // SOCKET over Wi-Fi
#else
  int fd_ = -1;
#endif
  std::string buf_;
};

}  // namespace rma

// Thin POSIX wrapper for a line connection to the robot: a USB CDC serial
// port (or a pty in tests), or a TCP socket over Wi-Fi.
#pragma once

#include <chrono>
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

  OpenResult open(const std::string& path);
  // TCP to a numeric IPv4 address; gives up after `timeout`.
  OpenResult open_tcp(const std::string& ip, int port, Clock::duration timeout);
  void close();
  bool is_open() const { return fd_ >= 0; }

  // Writes everything (handling short writes and EAGAIN) before the deadline.
  IoResult write_all(std::string_view data, Clock::time_point deadline);

  // Reads one line, without the trailing "\n" or "\r".
  IoResult read_line(std::string* line, Clock::time_point deadline);

 private:
  int fd_ = -1;
  std::string buf_;
};

}  // namespace rma

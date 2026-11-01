#include "serial_port.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>

namespace rma {
namespace {

int ms_until(Clock::time_point deadline) {
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - Clock::now())
                .count();
  return ms < 0 ? 0 : static_cast<int>(ms);
}

}  // namespace

OpenResult SerialPort::open(const std::string& path) {
  close();
  int fd = ::open(path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    if (errno == ENOENT || errno == ENXIO) return OpenResult::NotFound;
    if (errno == EBUSY) return OpenResult::Busy;
    return OpenResult::Error;
  }
  if (ioctl(fd, TIOCEXCL) != 0) {
    int err = errno;
    ::close(fd);
    return err == EBUSY ? OpenResult::Busy : OpenResult::Error;
  }
  termios tio{};
  if (tcgetattr(fd, &tio) != 0) {
    ::close(fd);
    return OpenResult::Error;
  }
  cfmakeraw(&tio);
  tio.c_cflag |= CLOCAL | CREAD | HUPCL;
  // Never 1200: the Pico SDK treats a 1200-baud setting as "reboot to BOOTSEL".
  cfsetspeed(&tio, B115200);
  if (tcsetattr(fd, TCSANOW, &tio) != 0) {
    ::close(fd);
    return OpenResult::Error;
  }
  tcflush(fd, TCIOFLUSH);
  fd_ = fd;
  buf_.clear();
  return OpenResult::Ok;
}

OpenResult SerialPort::open_tcp(const std::string& ip, int port, Clock::duration timeout) {
  close();
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) return OpenResult::NotFound;
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return OpenResult::Error;
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);  // a dead robot is an error, not a crash
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
    if (errno != EINPROGRESS) {
      ::close(fd);
      return OpenResult::NotFound;
    }
    pollfd p{fd, POLLOUT, 0};
    int err = 0;
    socklen_t len = sizeof err;
    if (::poll(&p, 1, ms_until(Clock::now() + timeout)) != 1 ||
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) != 0 || err != 0) {
      ::close(fd);
      return OpenResult::NotFound;
    }
  }
  fd_ = fd;
  buf_.clear();
  return OpenResult::Ok;
}

void SerialPort::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  buf_.clear();
}

IoResult SerialPort::write_all(std::string_view data, Clock::time_point deadline) {
  if (fd_ < 0) return IoResult::Dead;
  size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::write(fd_, data.data() + off, data.size() - off);
    if (n > 0) {
      off += static_cast<size_t>(n);
      continue;
    }
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && errno != EAGAIN) return IoResult::Dead;
    pollfd p{fd_, POLLOUT, 0};
    int r = ::poll(&p, 1, ms_until(deadline));
    if (r == 0) return IoResult::Timeout;
    if (r < 0 && errno != EINTR) return IoResult::Dead;
    if (r > 0 && (p.revents & (POLLHUP | POLLERR | POLLNVAL))) return IoResult::Dead;
  }
  return IoResult::Ok;
}

IoResult SerialPort::read_line(std::string* line, Clock::time_point deadline) {
  if (fd_ < 0) return IoResult::Dead;
  for (;;) {
    size_t nl = buf_.find('\n');
    if (nl != std::string::npos) {
      std::string out = buf_.substr(0, nl);
      buf_.erase(0, nl + 1);
      while (!out.empty() && out.back() == '\r') out.pop_back();
      *line = std::move(out);
      return IoResult::Ok;
    }
    pollfd p{fd_, POLLIN, 0};
    int r = ::poll(&p, 1, ms_until(deadline));
    if (r == 0) return IoResult::Timeout;
    if (r < 0) {
      if (errno == EINTR) continue;
      return IoResult::Dead;
    }
    if (p.revents & (POLLERR | POLLNVAL)) return IoResult::Dead;
    char tmp[256];
    ssize_t n = ::read(fd_, tmp, sizeof tmp);
    if (n > 0) {
      buf_.append(tmp, static_cast<size_t>(n));
      continue;
    }
    if (n == 0) return IoResult::Dead;  // EOF: device gone
    if (errno == EAGAIN || errno == EINTR) {
      if (p.revents & POLLHUP) return IoResult::Dead;
      continue;
    }
    return IoResult::Dead;  // ENXIO, EIO, ...
  }
}

}  // namespace rma

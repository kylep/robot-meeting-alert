#include "pairing.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>

namespace rma {
namespace {

// Writes `data` to dir/name readable only by this user (0600), atomically.
bool write_private(const std::string& name, const void* data, size_t len) {
  std::string dir = app_data_dir().string();
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  std::string path = dir + "/" + name, tmp = path + ".tmp";
  int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return false;
  bool ok = ::write(fd, data, len) == static_cast<ssize_t>(len);
  ::close(fd);
  return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace

std::filesystem::path app_data_dir() {
  const char* home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/Library/Application Support/RobotMeetingAlert";
}

std::string pairing_key_path() { return (app_data_dir() / "pairing.key").string(); }

bool load_pairing_key(uint8_t key[16]) {
  int fd = ::open(pairing_key_path().c_str(), O_RDONLY);
  if (fd < 0) return false;
  ssize_t n = ::read(fd, key, 16);
  ::close(fd);
  return n == 16;
}

bool save_pairing_key(const uint8_t key[16]) { return write_private("pairing.key", key, 16); }

std::string load_ics_link() {
  std::string path = (app_data_dir() / "ics.link").string();
  int fd = ::open(path.c_str(), O_RDONLY);
  if (fd < 0) return {};
  std::string out;
  char buf[512];
  ssize_t n;
  while ((n = ::read(fd, buf, sizeof buf)) > 0 && out.size() < 8192) out.append(buf, static_cast<size_t>(n));
  ::close(fd);
  while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
  return out;
}

bool save_ics_link(const std::string& link) {
  if (link.empty()) {
    std::string path = (app_data_dir() / "ics.link").string();
    return ::unlink(path.c_str()) == 0 || errno == ENOENT;
  }
  return write_private("ics.link", link.data(), link.size());
}

}  // namespace rma

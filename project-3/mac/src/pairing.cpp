#include "pairing.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>

namespace rma {

std::string pairing_key_path() {
  const char* home = std::getenv("HOME");
  return std::string(home ? home : ".") + "/Library/Application Support/RobotMeetingAlert/pairing.key";
}

bool load_pairing_key(uint8_t key[16]) {
  int fd = ::open(pairing_key_path().c_str(), O_RDONLY);
  if (fd < 0) return false;
  ssize_t n = ::read(fd, key, 16);
  ::close(fd);
  return n == 16;
}

bool save_pairing_key(const uint8_t key[16]) {
  std::string path = pairing_key_path();
  std::string dir = path.substr(0, path.rfind('/'));
  mkdir(dir.c_str(), 0755);
  std::string tmp = path + ".tmp";
  int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (fd < 0) return false;
  bool ok = ::write(fd, key, 16) == 16;
  ::close(fd);
  return ok && std::rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace rma

#include "pairing.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

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

bool load_or_create_pairing_key(uint8_t key[16]) {
  if (load_pairing_key(key)) return true;
  std::string path = pairing_key_path();
  std::string dir = path.substr(0, path.rfind('/'));
  mkdir(dir.c_str(), 0755);
  arc4random_buf(key, 16);
  int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (fd < 0) return load_pairing_key(key);  // another process made it first
  bool ok = ::write(fd, key, 16) == 16;
  ::close(fd);
  return ok;
}

}  // namespace rma

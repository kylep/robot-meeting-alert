#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace rma {

constexpr int kRaspberryPiVendorId = 0x2E8A;
constexpr int kRobotPort = 4242;

// Lists /dev/cu.* callout devices whose USB ancestor has the Raspberry Pi
// vendor ID. Unrelated serial devices are never returned, so never opened.
std::vector<std::string> find_pico_ports();

// Finds the robot on Wi-Fi. It announces itself as "rma.local" (mDNS); a
// background thread looks that name up every few seconds so the link never
// waits on it. An address typed by the user is tried first.
class RobotFinder {
 public:
  RobotFinder();
  ~RobotFinder();
  RobotFinder(const RobotFinder&) = delete;
  RobotFinder& operator=(const RobotFinder&) = delete;

  void set_manual(const std::string& ip);
  std::string found() const;  // last address "rma.local" resolved to, or ""
  // USB first (it is plugged in for a reason: setup), then Wi-Fi as "tcp:IP:PORT".
  std::vector<std::string> candidates() const;

 private:
  void run();
  mutable std::mutex mu_;
  std::string manual_, found_;
  std::atomic<bool> stop_{false};
  std::thread worker_;
};

}  // namespace rma

// Owns the serial connection on a worker thread: finds the Pico, keeps the
// device in sync with the desired outputs, and keeps the buzzer lease alive
// only while the GUI keeps saying "held".
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rma/device.hpp"
#include "serial_port.hpp"

namespace rma {

enum class LinkState { Searching, Connected };

struct LinkSnapshot {
  LinkState state = LinkState::Searching;
  std::string port;
  std::string fw;
  std::string last_error;
  bool port_busy = false;
  Outputs reported;
};

class Link {
 public:
  using Candidates = std::function<std::vector<std::string>()>;

  static constexpr auto kScanInterval = std::chrono::milliseconds(1000);
  static constexpr auto kHelloTimeout = std::chrono::milliseconds(500);
  static constexpr auto kReplyTimeout = std::chrono::milliseconds(500);
  static constexpr auto kCadence = std::chrono::milliseconds(20);
  static constexpr auto kBuzzRefresh = std::chrono::milliseconds(100);
  static constexpr auto kBuzzFreshness = std::chrono::milliseconds(150);
  static constexpr auto kIdleProbe = std::chrono::milliseconds(500);

  explicit Link(Candidates candidates);
  ~Link();
  Link(const Link&) = delete;
  Link& operator=(const Link&) = delete;

  void start();
  // Call every GUI frame. A held buzzer that stops being refreshed (GUI
  // stall) is released after kBuzzFreshness.
  void set_desired(const Outputs& o);
  LinkSnapshot snapshot() const;
  // Sends ALLOFF best-effort from the worker, closes the port, joins.
  void stop();

 private:
  void run();
  bool try_connect();
  void connected_loop();
  bool command(const std::string& cmd);
  bool wait_or_stop(Clock::duration d);
  void set_snapshot(const LinkSnapshot& s);

  Candidates candidates_;
  SerialPort port_;  // touched only by the worker thread
  std::thread worker_;
  std::atomic<bool> stop_{false};

  mutable std::mutex mu_;
  std::condition_variable cv_;
  Outputs desired_;
  Clock::time_point desired_stamp_{};
  LinkSnapshot snap_;

  // Worker-only state.
  Outputs reported_;
  Clock::time_point last_cmd_{};
  Clock::time_point last_buzz_{};
};

}  // namespace rma

// Owns the serial connection on a worker thread: finds the Pico, keeps the
// device's settings in step with the app, sends play/stop events in order,
// and polls the knob reading.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rma2/host.hpp"
#include "serial_port.hpp"

namespace rma2 {

using rma::Clock;

enum class LinkState { Searching, Connected };

// What the app wants. Reconciled onto the device whenever it differs.
struct Desired {
  bool light = false;
  uint8_t r = 255, g = 255, b = 255;
  uint8_t sound = 1;
  uint8_t meter = 0;
};

struct LinkSnapshot {
  LinkState state = LinkState::Searching;
  std::string port;
  std::string fw;
  std::string last_error;
  bool port_busy = false;
  State reported;  // last state line from the device
};

class Link {
 public:
  using Candidates = std::function<std::vector<std::string>()>;

  static constexpr auto kScanInterval = std::chrono::milliseconds(1000);
  static constexpr auto kHelloTimeout = std::chrono::milliseconds(500);
  static constexpr auto kReplyTimeout = std::chrono::milliseconds(500);
  static constexpr auto kCadence = std::chrono::milliseconds(20);
  static constexpr auto kPoll = std::chrono::milliseconds(100);  // knob reading

  explicit Link(Candidates candidates);
  ~Link();
  Link(const Link&) = delete;
  Link& operator=(const Link&) = delete;

  void start();
  void set_desired(const Desired& d);
  void play(int sound = -1);  // -1 = the selected sound ("PLAY"); else "PLAY n"
  void stop_sound();
  LinkSnapshot snapshot() const;
  // Silences the buzzer and turns the light off (best effort), then closes.
  void stop();

 private:
  void run();
  bool try_connect();
  void connected_loop();
  bool command(const std::string& cmd);
  bool wait_or_stop(Clock::duration d);

  Candidates candidates_;
  rma::SerialPort port_;  // worker thread only
  std::thread worker_;
  std::atomic<bool> stop_{false};

  mutable std::mutex mu_;
  std::condition_variable cv_;
  Desired desired_;
  std::deque<std::string> events_;
  LinkSnapshot snap_;

  State reported_;  // worker only
  Clock::time_point last_cmd_{};
};

}  // namespace rma2

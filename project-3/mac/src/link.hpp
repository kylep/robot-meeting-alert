// Owns the connection to the robot (USB serial or TCP over Wi-Fi) on a
// worker thread: finds it, keeps its settings in step with the app, sends
// play/stop events in order, pushes the calendar schedule, and polls the
// knob reading and Wi-Fi status.
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "rma3/auth.hpp"
#include "rma3/host.hpp"
#include "serial_port.hpp"

namespace rma3 {

using rma::Clock;

enum class LinkState { Searching, Connected };

// What the app wants. Reconciled onto the device whenever it differs.
struct Desired {
  bool light = true;
  uint8_t r = 255, g = 255, b = 255;
  uint8_t sound = 1;
  uint8_t meter = 0;
  uint8_t mode = 1;  // 0 manual, 1 calendar
};

struct LinkSnapshot {
  LinkState state = LinkState::Searching;
  std::string port;  // "/dev/cu..." or "tcp:IP:PORT"
  bool over_wifi = false;
  std::string fw;
  std::string last_error;
  bool port_busy = false;
  State reported;  // last state line from the device
  uint8_t net = 0;     // robot's Wi-Fi status (NET)
  std::string ip;      // robot's address on Wi-Fi, or "-"
  int pushed = -1;     // meetings in the last schedule push (-1 = none yet)
  std::string schedule_error;
  std::string wifi_result;  // outcome of the last Wi-Fi setup
  bool not_paired = false;  // the robot on Wi-Fi didn't accept this Mac
};

class Link {
 public:
  using Candidates = std::function<std::vector<std::string>()>;
  using WallClock = std::function<double()>;  // seconds since 1970

  static constexpr auto kScanInterval = std::chrono::milliseconds(1000);
  static constexpr auto kHelloTimeout = std::chrono::milliseconds(800);
  static constexpr auto kReplyTimeout = std::chrono::milliseconds(800);
  static constexpr auto kConnectTimeout = std::chrono::milliseconds(700);
  static constexpr auto kCadence = std::chrono::milliseconds(20);
  static constexpr auto kPoll = std::chrono::milliseconds(100);  // knob reading
  static constexpr auto kNetPoll = std::chrono::seconds(5);
  static constexpr auto kPushEvery = std::chrono::seconds(30);
  static constexpr int kValidS = 900;  // robot goes dim blue after 15 min without a push

  explicit Link(Candidates candidates, WallClock wall = nullptr);
  ~Link();
  Link(const Link&) = delete;
  Link& operator=(const Link&) = delete;

  void start();
  void set_desired(const Desired& d);
  void play(int sound = -1);  // -1 = the selected sound ("PLAY"); else "PLAY n"
  void stop_sound();
  // The calendar events to schedule from; disabled = never push a schedule.
  void set_calendar(bool enabled, std::vector<CalEvent> events);
  // Save Wi-Fi settings on the robot (works over USB only).
  void wifi_setup(const std::string& ssid, const std::string& pass);
  void wifi_clear();
  // This Mac's pairing key: given to the robot over USB, proven over Wi-Fi.
  void set_key(const uint8_t key[rma3::kKeyLen]);
  LinkSnapshot snapshot() const;
  void stop();

 private:
  enum class Reply { Ok, Err, Dead };
  void run();
  bool try_connect();
  void connected_loop();
  Reply send(const std::string& cmd, std::string* line);
  Reply command(const std::string& cmd);  // expects a state line
  bool push_schedule();                   // false = connection dead
  bool do_wifi();                         // false = connection dead
  bool prove_key();                       // Wi-Fi: answer the robot's challenge
  void give_key();                        // USB: pair the robot with this Mac
  bool wait_or_stop(Clock::duration d);

  Candidates candidates_;
  WallClock wall_;
  rma::SerialPort port_;  // worker thread only
  std::thread worker_;
  std::atomic<bool> stop_{false};

  mutable std::mutex mu_;
  std::condition_variable cv_;
  Desired desired_;
  std::deque<std::string> events_;
  bool cal_enabled_ = false;
  std::vector<CalEvent> cal_events_;
  uint64_t cal_gen_ = 0;
  bool wifi_pending_ = false, wifi_clear_ = false;
  std::string wifi_ssid_, wifi_pass_;
  uint8_t key_[rma3::kKeyLen] = {};
  bool has_key_ = false;
  LinkSnapshot snap_;

  // Worker only.
  State reported_;
  bool over_tcp_ = false;
  std::string rejected_;  // last setting the robot refused
  Clock::time_point last_cmd_{}, last_net_{}, last_push_{};
  uint64_t pushed_gen_ = 0;
};

}  // namespace rma3

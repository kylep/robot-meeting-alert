#include "link.hpp"

#include <cstdio>

namespace rma2 {

using rma::IoResult;
using rma::OpenResult;

Link::Link(Candidates candidates) : candidates_(std::move(candidates)) {}

Link::~Link() { stop(); }

void Link::start() {
  if (worker_.joinable()) return;
  stop_ = false;
  worker_ = std::thread([this] { run(); });
}

void Link::set_desired(const Desired& d) {
  std::lock_guard<std::mutex> lock(mu_);
  desired_ = d;
}

void Link::play(int sound) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    events_.push_back(sound < 0 ? std::string("PLAY") : "PLAY " + std::to_string(sound));
  }
  cv_.notify_all();
}

void Link::stop_sound() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    events_.push_back("STOP");
  }
  cv_.notify_all();
}

LinkSnapshot Link::snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return snap_;
}

void Link::stop() {
  {
    std::lock_guard<std::mutex> lock(mu_);
    stop_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

bool Link::wait_or_stop(Clock::duration d) {
  std::unique_lock<std::mutex> lock(mu_);
  return cv_.wait_for(lock, d, [this] { return stop_.load(); });
}

void Link::run() {
  while (!stop_) {
    if (try_connect()) {
      connected_loop();
      if (port_.is_open()) {
        if (stop_) {
          auto dl = Clock::now() + std::chrono::milliseconds(100);
          port_.write_all("STOP\nLIGHT 0\n", dl);
        }
        port_.close();
      }
      std::lock_guard<std::mutex> lock(mu_);
      snap_.state = LinkState::Searching;
      snap_.port.clear();
      snap_.fw.clear();
      continue;
    }
    if (wait_or_stop(kScanInterval)) break;
  }
}

bool Link::try_connect() {
  bool busy = false;
  std::string err;
  for (const std::string& path : candidates_()) {
    if (stop_) return false;
    OpenResult r = port_.open(path);
    if (r == OpenResult::Busy) {
      busy = true;
      err = "port busy: " + path;
      continue;
    }
    if (r != OpenResult::Ok) continue;
    auto deadline = Clock::now() + kHelloTimeout;
    std::string fw;
    bool ok = port_.write_all("\nHELLO\n", deadline) == IoResult::Ok;
    while (ok) {
      std::string line;
      if (port_.read_line(&line, deadline) != IoResult::Ok) {
        ok = false;
        break;
      }
      if (parse_hello(line, &fw)) break;
    }
    if (!ok) {
      port_.close();
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(mu_);
      snap_ = LinkSnapshot{};
      snap_.state = LinkState::Connected;
      snap_.port = path;
      snap_.fw = fw;
      events_.clear();  // never replay stale clicks after a reconnect
    }
    if (!command("STATE")) {  // learn the device's settings; reconcile pushes ours
      port_.close();
      return false;
    }
    return true;
  }
  std::lock_guard<std::mutex> lock(mu_);
  snap_.port_busy = busy;
  snap_.last_error = err;
  return false;
}

bool Link::command(const std::string& cmd) {
  auto deadline = Clock::now() + kReplyTimeout;
  if (port_.write_all(cmd + "\n", deadline) != IoResult::Ok) return false;
  last_cmd_ = Clock::now();
  std::string line;
  for (;;) {
    if (port_.read_line(&line, deadline) != IoResult::Ok) return false;
    if (line.empty()) continue;
    State s;
    if (!parse_state(line, &s)) return false;  // ERR or garbage
    reported_ = s;
    std::lock_guard<std::mutex> lock(mu_);
    snap_.reported = s;
    return true;
  }
}

void Link::connected_loop() {
  char buf[32];
  while (!stop_) {
    Desired d;
    std::deque<std::string> events;
    {
      std::lock_guard<std::mutex> lock(mu_);
      d = desired_;
      events.swap(events_);
    }
    // Settings first, so a preview queued after a selection hears the new one.
    const State& r = reported_;
    if (d.light != r.light && !command(d.light ? "LIGHT 1" : "LIGHT 0")) return;
    if ((d.r != r.r || d.g != r.g || d.b != r.b)) {
      std::snprintf(buf, sizeof buf, "RGB %d %d %d", d.r, d.g, d.b);
      if (!command(buf)) return;
    }
    if (d.sound != r.sound && !command("SOUND " + std::to_string(d.sound))) return;
    if (d.meter != r.meter && !command("METER " + std::to_string(d.meter))) return;
    for (const std::string& e : events) {
      if (!command(e)) return;
    }
    if (Clock::now() - last_cmd_ >= kPoll && !command("STATE")) return;
    if (wait_or_stop(kCadence)) return;
  }
}

}  // namespace rma2

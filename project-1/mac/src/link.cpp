#include "link.hpp"

#include "rma/host.hpp"

namespace rma {

Link::Link(Candidates candidates) : candidates_(std::move(candidates)) {}

Link::~Link() { stop(); }

void Link::start() {
  if (worker_.joinable()) return;
  stop_ = false;
  worker_ = std::thread([this] { run(); });
}

void Link::set_desired(const Outputs& o) {
  std::lock_guard<std::mutex> lock(mu_);
  desired_ = o;
  desired_stamp_ = Clock::now();
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

void Link::set_snapshot(const LinkSnapshot& s) {
  std::lock_guard<std::mutex> lock(mu_);
  snap_ = s;
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
          port_.write_all("ALLOFF\n", Clock::now() + std::chrono::milliseconds(100));
        }
        port_.close();
      }
      LinkSnapshot s = snapshot();
      s.state = LinkState::Searching;
      s.port.clear();
      s.fw.clear();
      set_snapshot(s);
      continue;  // rescan immediately after a drop
    }
    if (wait_or_stop(kScanInterval)) break;
  }
}

bool Link::try_connect() {
  LinkSnapshot s;
  for (const std::string& path : candidates_()) {
    if (stop_) return false;
    OpenResult r = port_.open(path);
    if (r == OpenResult::Busy) {
      s.port_busy = true;
      s.last_error = "port busy: " + path;
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
      if (parse_hello_reply(line, &fw)) break;  // ignore stray lines
    }
    if (!ok) {
      port_.close();
      continue;
    }
    reported_ = Outputs{};
    last_buzz_ = Clock::time_point{};
    s = LinkSnapshot{};
    s.state = LinkState::Connected;
    s.port = path;
    s.fw = fw;
    set_snapshot(s);
    if (!command("ALLOFF")) {
      port_.close();
      return false;
    }
    return true;
  }
  LinkSnapshot cur = snapshot();
  cur.port_busy = s.port_busy;
  cur.last_error = s.last_error;
  set_snapshot(cur);
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
    Outputs o;
    if (!parse_state_reply(line, &o)) return false;  // ERR or garbage
    reported_ = o;
    std::lock_guard<std::mutex> lock(mu_);
    snap_.reported = o;
    return true;
  }
}

void Link::connected_loop() {
  static const char kLetters[3] = {'R', 'G', 'B'};
  while (!stop_) {
    Outputs d;
    Clock::time_point stamp;
    {
      std::lock_guard<std::mutex> lock(mu_);
      d = desired_;
      stamp = desired_stamp_;
    }
    auto now = Clock::now();
    bool buzz = d.buzz && (now - stamp) <= kBuzzFreshness;

    bool sent = false;
    const bool want[3] = {d.r, d.g, d.b};
    const bool have[3] = {reported_.r, reported_.g, reported_.b};
    for (int i = 0; i < 3; ++i) {
      if (want[i] == have[i]) continue;
      std::string cmd = std::string("LED ") + kLetters[i] + (want[i] ? " 1" : " 0");
      if (!command(cmd)) return;
      sent = true;
    }
    if (buzz && (!reported_.buzz || now - last_buzz_ >= kBuzzRefresh)) {
      if (!command("BUZZ 1")) return;
      last_buzz_ = now;
      sent = true;
    } else if (!buzz && reported_.buzz) {
      if (!command("BUZZ 0")) return;
      sent = true;
    }
    if (!sent && Clock::now() - last_cmd_ >= kIdleProbe) {
      if (!command("STATE")) return;
    }
    if (wait_or_stop(kCadence)) return;
  }
}

}  // namespace rma

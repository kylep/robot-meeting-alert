#include "link.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace rma3 {

using rma::IoResult;
using rma::OpenResult;

namespace {

double system_seconds() {
  using namespace std::chrono;
  return duration_cast<duration<double>>(system_clock::now().time_since_epoch()).count();
}

// "tcp:IP:PORT" -> ip, port.
bool split_tcp(const std::string& c, std::string* ip, int* port) {
  if (c.rfind("tcp:", 0) != 0) return false;
  size_t colon = c.rfind(':');
  if (colon <= 4) return false;
  *ip = c.substr(4, colon - 4);
  *port = std::atoi(c.c_str() + colon + 1);
  return *port > 0;
}

}  // namespace

Link::Link(Candidates candidates, WallClock wall)
    : candidates_(std::move(candidates)), wall_(wall ? std::move(wall) : system_seconds) {}

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

void Link::set_calendar(bool enabled, std::vector<CalEvent> events) {
  std::lock_guard<std::mutex> lock(mu_);
  cal_enabled_ = enabled;
  cal_events_ = std::move(events);
  ++cal_gen_;
}

void Link::wifi_setup(const std::string& ssid, const std::string& pass) {
  std::lock_guard<std::mutex> lock(mu_);
  wifi_ssid_ = ssid;
  wifi_pass_ = pass;
  wifi_clear_ = false;
  wifi_pending_ = true;
  snap_.wifi_result = "Sending...";
}

void Link::wifi_clear() {
  std::lock_guard<std::mutex> lock(mu_);
  wifi_clear_ = true;
  wifi_pending_ = true;
  snap_.wifi_result = "Sending...";
}

void Link::set_key(const uint8_t key[kKeyLen]) {
  std::lock_guard<std::mutex> lock(mu_);
  std::memcpy(key_, key, kKeyLen);
  has_key_ = true;
}

void Link::set_key_on_robot() {
  std::lock_guard<std::mutex> lock(mu_);
  key_pending_ = true;
  snap_.key_result = "Sending...";
}

LinkSnapshot Link::snapshot() const {
  std::lock_guard<std::mutex> lock(mu_);
  return snap_;
}

// Quitting leaves the robot as it is: in calendar mode it carries on alerting
// from the schedule it has (until that goes stale). It silences a manual
// preview by itself when the connection closes.
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
      port_.close();
      std::lock_guard<std::mutex> lock(mu_);
      snap_.state = LinkState::Searching;
      snap_.port.clear();
      snap_.fw.clear();
      snap_.over_wifi = false;
      continue;
    }
    if (wait_or_stop(kScanInterval)) break;
  }
}

bool Link::try_connect() {
  bool busy = false;
  std::string err;
  for (const std::string& target : candidates_()) {
    if (stop_) return false;
    std::string ip;
    int tcp_port = 0;
    bool tcp = split_tcp(target, &ip, &tcp_port);
    OpenResult r = tcp ? port_.open_tcp(ip, tcp_port, kConnectTimeout) : port_.open(target);
    session_ = NetSession{};  // a fresh connection starts untrusted
    if (r == OpenResult::Busy) {
      busy = true;
      err = "port busy: " + target;
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
    over_tcp_ = tcp;
    if (tcp && !prove_key()) {  // a robot on Wi-Fi only obeys its paired Mac
      port_.close();
      std::lock_guard<std::mutex> lock(mu_);
      snap_.not_paired = true;
      err = "The robot on Wi-Fi didn't accept this Mac's passphrase.";
      continue;
    }
    {
      std::lock_guard<std::mutex> lock(mu_);
      std::string wifi_result = snap_.wifi_result, key_result = snap_.key_result;
      snap_ = LinkSnapshot{};
      snap_.key_result = key_result;
      snap_.state = LinkState::Connected;
      snap_.port = target;
      snap_.over_wifi = tcp;
      snap_.fw = fw;
      snap_.wifi_result = wifi_result;
      events_.clear();  // never replay stale clicks after a reconnect
      wifi_pending_ = false;  // nor a Wi-Fi setup typed for an earlier connection
      wifi_ssid_.clear();
      wifi_pass_.assign(wifi_pass_.size(), '\0');
      wifi_pass_.clear();
    }
    last_net_ = last_push_ = Clock::time_point{};
    rejected_.clear();
    pushed_gen_ = 0;
    if (command("STATE") != Reply::Ok) {  // learn the device's settings; reconcile pushes ours
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

Link::Reply Link::send(const std::string& cmd, std::string* line) {
  auto deadline = Clock::now() + kReplyTimeout;
  if (port_.write_all(session_.sign(cmd) + "\n", deadline) != IoResult::Ok) return Reply::Dead;
  last_cmd_ = Clock::now();
  for (;;) {
    if (port_.read_line(line, deadline) != IoResult::Ok) return Reply::Dead;
    if (!line->empty()) break;
  }
  return line->rfind("ERR", 0) == 0 ? Reply::Err : Reply::Ok;
}

bool Link::prove_key() {
  uint8_t key[kKeyLen];
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!has_key_) return false;
    std::memcpy(key, key_, kKeyLen);
  }
  std::string line, auth;
  if (send(session_.hello(), &line) != Reply::Ok) return false;
  // The robot proves itself first; an impostor gets nothing it could relay.
  if (!session_.answer(key, line, &auth)) return false;
  if (send(auth, &line) != Reply::Ok || line != "OK AUTH") return false;
  session_.start();
  return true;
}

bool Link::give_key() {
  uint8_t key[kKeyLen];
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!key_pending_) return true;
    key_pending_ = false;
    if (!has_key_) {
      snap_.key_result = "Type a passphrase first.";
      return true;
    }
    std::memcpy(key, key_, kKeyLen);
  }
  std::string result;
  if (over_tcp_) {
    result = "Plug the robot in with USB to change its passphrase.";
  } else {
    char hex[2 * kKeyLen + 1];
    to_hex(key, kKeyLen, hex);
    std::string line;
    Reply r = send(std::string("KEY ") + hex, &line);
    if (r == Reply::Dead) return false;
    result = r == Reply::Ok ? "The robot now uses this passphrase." : "The robot's firmware is too old (make flash).";
  }
  std::lock_guard<std::mutex> lock(mu_);
  snap_.key_result = result;
  return true;
}

Link::Reply Link::command(const std::string& cmd) {
  std::string line;
  Reply r = send(cmd, &line);
  if (r != Reply::Ok) return r;
  State s;
  if (!parse_state(line, &s)) return Reply::Dead;  // garbage: resync by reconnecting
  reported_ = s;
  std::lock_guard<std::mutex> lock(mu_);
  snap_.reported = s;
  return Reply::Ok;
}

bool Link::push_schedule() {
  std::vector<CalEvent> events;
  uint64_t gen = 0;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!cal_enabled_) return true;
    events = cal_events_;
    gen = cal_gen_;
  }
  bool due = gen != pushed_gen_ || last_push_ == Clock::time_point{} || Clock::now() - last_push_ >= kPushEvery;
  if (!due) return true;
  last_push_ = Clock::now();
  pushed_gen_ = gen;
  // Offsets are taken at one instant, just before CAL (the robot's time base).
  std::vector<Pushed> ms = pick_meetings(wall_(), events);
  std::string error;
  auto step = [&](const std::string& cmd) {
    Reply r = command(cmd);
    if (r == Reply::Err) error = "robot refused \"" + cmd + "\"";
    return r;
  };
  Reply r = step("CAL");
  for (size_t i = 0; r == Reply::Ok && i < ms.size(); ++i) {
    r = step("EV " + std::to_string(ms[i].id) + " " + std::to_string(ms[i].start) + " " +
             std::to_string(ms[i].end));
  }
  if (r == Reply::Ok) r = step("CALGO " + std::to_string(kValidS));
  if (r == Reply::Dead) return false;
  std::lock_guard<std::mutex> lock(mu_);
  snap_.schedule_error = error;  // an error never drops the connection; retried next time
  if (r == Reply::Ok) snap_.pushed = static_cast<int>(ms.size());
  return true;
}

bool Link::do_wifi() {
  std::string ssid, pass;
  bool clear = false;
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!wifi_pending_) return true;
    wifi_pending_ = false;
    ssid.swap(wifi_ssid_);
    pass.swap(wifi_pass_);
    clear = wifi_clear_;
  }
  // Never put the password on the network: Wi-Fi setup only travels over USB.
  if (over_tcp_) {
    pass.assign(pass.size(), '\0');
    std::lock_guard<std::mutex> lock(mu_);
    snap_.wifi_result = "Plug the robot in with USB to change Wi-Fi.";
    return true;
  }
  std::string result;
  Reply r = Reply::Ok;
  std::string line;
  if (clear) {
    r = send("WIFICLR", &line);
    result = r == Reply::Ok ? "Wi-Fi settings erased." : "";
  } else {
    r = send("WIFI " + ssid, &line);
    if (r == Reply::Ok) r = send("PASS " + pass, &line);  // the robot never echoes it
    if (r == Reply::Ok) r = send("WIFIGO", &line);
    result = r == Reply::Ok ? "Saved. The robot is joining \"" + ssid + "\"." : "";
  }
  pass.assign(pass.size(), '\0');
  if (r == Reply::Err) {
    result = line == "ERR USBONLY" ? "Plug the robot in with USB to change Wi-Fi."
                                   : "The robot rejected that (name 1-32 chars, password 8-63 or empty).";
  }
  if (r == Reply::Dead) return false;
  std::lock_guard<std::mutex> lock(mu_);
  snap_.wifi_result = result;
  return true;
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
    // A setting the robot refuses (e.g. a sound older firmware doesn't know)
    // is reported and not retried until it changes; it never drops the link.
    auto setting = [&](bool differs, const std::string& cmd) {
      if (!differs || cmd == rejected_) return true;
      Reply rep = command(cmd);
      if (rep == Reply::Err) {
        rejected_ = cmd;
        std::lock_guard<std::mutex> lock(mu_);
        snap_.last_error = "The robot refused \"" + cmd + "\". Update its firmware (make flash over USB).";
      }
      return rep != Reply::Dead;
    };
    std::snprintf(buf, sizeof buf, "RGB %d %d %d", d.r, d.g, d.b);
    if (!setting(d.light != r.light, d.light ? "LIGHT 1" : "LIGHT 0")) return;
    if (!setting(d.r != r.r || d.g != r.g || d.b != r.b, buf)) return;
    if (!setting(d.sound != r.sound, "SOUND " + std::to_string(d.sound))) return;
    if (!setting(d.meter != r.meter, "METER " + std::to_string(d.meter))) return;
    if (!setting(d.mode != r.mode, "MODE " + std::to_string(d.mode))) return;
    for (const std::string& e : events) {
      if (command(e) == Reply::Dead) return;
    }
    if (!do_wifi()) return;
    if (!give_key()) return;
    if (!push_schedule()) return;
    if (Clock::now() - last_net_ >= kNetPoll) {
      std::string line;
      if (send("NET", &line) == Reply::Dead) return;
      last_net_ = Clock::now();
      int st = 0;
      std::string ip;
      if (parse_net(line, &st, &ip)) {
        std::lock_guard<std::mutex> lock(mu_);
        snap_.net = static_cast<uint8_t>(st);
        snap_.ip = ip;
      }
    }
    if (Clock::now() - last_cmd_ >= kPoll && command("STATE") != Reply::Ok) return;
    if (wait_or_stop(kCadence)) return;
  }
}

}  // namespace rma3

// Link against fake robots that run the real rma3::Device: one on a pty
// (USB) and one on a loopback TCP socket (Wi-Fi).
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#include <util.h>  // openpty (macOS)

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "check.hpp"
#include "link.hpp"
#include "rma3/device.hpp"

using namespace std::chrono_literals;
using rma::Clock;

namespace {

uint32_t now_ms() {
  return static_cast<uint32_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
}

// The robot's logic plus a log of every line received.
class FakeCore {
 public:
  explicit FakeCore(rma3::Source src) : src_(src) {}
  void set_silent(bool s) { silent_ = s; }
  void set_fail_ev(bool f) { fail_ev_ = f; }
  void set_knob_raw(uint16_t raw) { knob_raw_ = raw; }

  // Feeds received bytes; returns the reply bytes to send back.
  std::string handle(const char* buf, ssize_t n) {
    std::lock_guard<std::mutex> lock(mu_);
    dev_.knob_sample(knob_raw_);
    dev_.tick(now_ms(), true);
    std::string out;
    for (ssize_t i = 0; i < n; ++i) {
      char c = buf[i];
      if (c == '\n') {
        lines_.push_back(cur_);
        bool fail = fail_ev_ && cur_.rfind("EV", 0) == 0;
        cur_.clear();
        if (fail) {
          char scratch[rma3::Device::kMaxReply];
          dev_.feed(src_, '\n', now_ms(), scratch);  // keep the device's own buffer in step
          out += "ERR FULL\r\n";
          continue;
        }
      } else if (c != '\r') {
        cur_ += c;
      }
      if (silent_) continue;
      char reply[rma3::Device::kMaxReply];
      if (dev_.feed(src_, c, now_ms(), reply)) out += std::string(reply) + "\r\n";
    }
    return out;
  }
  void tick() {
    std::lock_guard<std::mutex> lock(mu_);
    dev_.tick(now_ms(), true);
  }

  rma3::Settings settings() {
    std::lock_guard<std::mutex> lock(mu_);
    return dev_.settings();
  }
  bool playing() {
    std::lock_guard<std::mutex> lock(mu_);
    return dev_.playing();
  }
  rma3::Stage stage() {
    std::lock_guard<std::mutex> lock(mu_);
    return dev_.stage();
  }
  int count(const std::string& line) {
    std::lock_guard<std::mutex> lock(mu_);
    int n = 0;
    for (auto& l : lines_) n += (l == line);
    return n;
  }
  int count_prefix(const std::string& p) {
    std::lock_guard<std::mutex> lock(mu_);
    int n = 0;
    for (auto& l : lines_) n += (l.rfind(p, 0) == 0);
    return n;
  }
  rma3::WifiAction take_wifi() {
    std::lock_guard<std::mutex> lock(mu_);
    return dev_.take_wifi_action(nullptr);
  }

 private:
  rma3::Source src_;
  std::atomic<bool> silent_{false}, fail_ev_{false};
  std::atomic<uint16_t> knob_raw_{0};
  std::mutex mu_;
  rma3::Device dev_;
  std::string cur_;
  std::vector<std::string> lines_;
};

class FakeUsb : public FakeCore {
 public:
  FakeUsb() : FakeCore(rma3::Source::Usb) {
    char name[128];
    if (openpty(&master_, &slave_, name, nullptr, nullptr) != 0) {
      std::perror("openpty");
      std::abort();
    }
    path_ = name;
    termios tio{};
    tcgetattr(slave_, &tio);
    cfmakeraw(&tio);
    tcsetattr(slave_, TCSANOW, &tio);
    fcntl(master_, F_SETFL, fcntl(master_, F_GETFL) | O_NONBLOCK);
    thread_ = std::thread([this] { run(); });
  }
  ~FakeUsb() {
    done_ = true;
    thread_.join();
    close(master_);
    close(slave_);
  }
  const std::string& path() const { return path_; }

 private:
  void run() {
    while (!done_) {
      pollfd p{master_, POLLIN, 0};
      ::poll(&p, 1, 5);
      char buf[256];
      ssize_t n = ::read(master_, buf, sizeof buf);
      std::string out = handle(buf, n < 0 ? 0 : n);
      if (!out.empty()) (void)!::write(master_, out.data(), out.size());
    }
  }
  int master_ = -1, slave_ = -1;
  std::string path_;
  std::thread thread_;
  std::atomic<bool> done_{false};
};

class FakeWifi : public FakeCore {
 public:
  FakeWifi() : FakeCore(rma3::Source::Net) {
    listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (::bind(listen_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(listen_, 1) != 0) {
      std::perror("bind/listen");
      std::abort();
    }
    socklen_t len = sizeof a;
    getsockname(listen_, reinterpret_cast<sockaddr*>(&a), &len);
    target_ = "tcp:127.0.0.1:" + std::to_string(ntohs(a.sin_port));
    thread_ = std::thread([this] { run(); });
  }
  ~FakeWifi() {
    done_ = true;
    thread_.join();
    if (client_ >= 0) close(client_);
    close(listen_);
  }
  const std::string& target() const { return target_; }

 private:
  void run() {
    while (!done_) {
      pollfd p[2] = {{listen_, POLLIN, 0}, {client_, POLLIN, 0}};
      ::poll(p, client_ >= 0 ? 2 : 1, 5);
      tick();
      if (p[0].revents & POLLIN) {
        int c = ::accept(listen_, nullptr, nullptr);
        if (client_ >= 0) close(client_);  // newest connection wins, like the robot
        client_ = c;
        continue;
      }
      if (client_ >= 0 && (p[1].revents & (POLLIN | POLLHUP))) {
        char buf[256];
        ssize_t n = ::read(client_, buf, sizeof buf);
        if (n <= 0) {
          close(client_);
          client_ = -1;
          continue;
        }
        std::string out = handle(buf, n);
        if (!out.empty()) (void)!::write(client_, out.data(), out.size());
      }
    }
  }
  int listen_ = -1, client_ = -1;
  std::string target_;
  std::thread thread_;
  std::atomic<bool> done_{false};
};

template <typename F>
bool eventually(F pred, Clock::duration within) {
  auto end = Clock::now() + within;
  while (Clock::now() < end) {
    if (pred()) return true;
    std::this_thread::sleep_for(5ms);
  }
  return pred();
}

bool connected(rma3::Link& l) { return l.snapshot().state == rma3::LinkState::Connected; }

void test_settings_sound_and_knob() {
  FakeUsb fake;
  rma3::Link link([&] { return std::vector<std::string>{fake.path()}; });
  link.start();
  CHECK(eventually([&] { return connected(link); }, 2s));
  CHECK_STR(link.snapshot().fw, "0.3.0");
  CHECK(!link.snapshot().over_wifi);

  rma3::Desired d;
  d.light = true;
  d.r = 10; d.g = 200; d.b = 30;
  d.sound = 2;
  d.meter = 3;
  d.mode = 0;
  link.set_desired(d);
  CHECK(eventually([&] {
    rma3::Settings s = fake.settings();
    return s.light && s.r == 10 && s.g == 200 && s.b == 30 && s.sound == 2 && s.meter == 3 && s.mode == 0;
  }, 500ms));

  // Preview another sound without changing the selection.
  link.play(4);
  CHECK(eventually([&] { return fake.playing(); }, 300ms));
  CHECK(fake.settings().sound == 2);
  CHECK(fake.count("PLAY 4") == 1);
  link.stop_sound();
  CHECK(eventually([&] { return !fake.playing(); }, 300ms));

  // The knob reading flows back through the STATE poll.
  fake.set_knob_raw(4095);
  CHECK(eventually([&] { return link.snapshot().reported.knob == 100; }, 500ms));
  fake.set_knob_raw(0);
  CHECK(eventually([&] { return link.snapshot().reported.knob == 0; }, 500ms));

  link.stop();
  CHECK(fake.settings().light);  // quitting the app leaves the robot running
}

void test_reconnect_never_plays_stale_clicks() {
  FakeUsb fake;
  std::atomic<bool> plugged{false};
  rma3::Link link([&] { return plugged ? std::vector<std::string>{fake.path()} : std::vector<std::string>{}; });
  link.start();
  link.play(1);  // clicked while unplugged
  std::this_thread::sleep_for(50ms);
  plugged = true;
  CHECK(eventually([&] { return connected(link); }, 2500ms));
  std::this_thread::sleep_for(300ms);
  CHECK(fake.count("PLAY 1") == 0);
  CHECK(!fake.playing());
}

void test_silent_device_drops_connection() {
  FakeUsb fake;
  rma3::Link link([&] { return std::vector<std::string>{fake.path()}; });
  link.start();
  CHECK(eventually([&] { return connected(link); }, 2s));
  fake.set_silent(true);
  CHECK(eventually([&] { return !connected(link); }, 2500ms));
}

double fake_now() { return 1'800'000'000.0; }

void test_wifi_schedule_push() {
  FakeWifi fake;
  rma3::Link link([&] { return std::vector<std::string>{fake.target()}; }, fake_now);
  rma3::CalEvent e;
  e.id = "standup";
  e.start = fake_now() + 400;
  e.end = fake_now() + 1000;
  link.set_calendar(true, {e});
  link.start();
  CHECK(eventually([&] { return connected(link); }, 2s));
  CHECK(link.snapshot().over_wifi);
  CHECK(eventually([&] { return fake.stage() == rma3::kStageFree; }, 1s));  // meeting in 400 s
  CHECK(eventually([&] { return link.snapshot().pushed == 1; }, 1s));
  CHECK(fake.count_prefix("EV ") == 1);
  // A calendar change pushes again at once: meeting now 2 min away = soon.
  e.start = fake_now() + 120;
  link.set_calendar(true, {e});
  CHECK(eventually([&] { return fake.stage() == rma3::kStageSoon; }, 1s));
  CHECK(eventually([&] { return link.snapshot().ip == "-"; }, 1s));  // NET polled
  // Wi-Fi settings can't be changed over Wi-Fi; the link stays up.
  link.wifi_setup("Home", "password123");
  CHECK(eventually([&] { return link.snapshot().wifi_result.find("USB") != std::string::npos; }, 1s));
  CHECK(fake.take_wifi() == rma3::WifiAction::None);
  CHECK(fake.count_prefix("PASS") == 0);  // the password never crossed the network
  CHECK(fake.count_prefix("WIFI") == 0);
  CHECK(connected(link));
}

void test_schedule_error_keeps_connection() {
  FakeWifi fake;
  fake.set_fail_ev(true);
  rma3::Link link([&] { return std::vector<std::string>{fake.target()}; }, fake_now);
  rma3::CalEvent e;
  e.id = "x";
  e.start = fake_now() + 60;
  e.end = fake_now() + 120;
  link.set_calendar(true, {e});
  link.start();
  CHECK(eventually([&] { return !link.snapshot().schedule_error.empty(); }, 2s));
  std::this_thread::sleep_for(200ms);
  CHECK(connected(link));
  CHECK(fake.count("HELLO") == 1);  // never reconnected
}

void test_wifi_setup_over_usb() {
  FakeUsb fake;
  rma3::Link link([&] { return std::vector<std::string>{fake.path()}; });
  link.start();
  CHECK(eventually([&] { return connected(link); }, 2s));
  link.wifi_setup("My Net", "password123");
  CHECK(eventually([&] { return link.snapshot().wifi_result.rfind("Saved", 0) == 0; }, 1s));
  CHECK(fake.take_wifi() == rma3::WifiAction::Save);
  CHECK(fake.count("WIFI My Net") == 1);
}

}  // namespace

int main() {
  test_settings_sound_and_knob();
  test_reconnect_never_plays_stale_clicks();
  test_silent_device_drops_connection();
  test_wifi_schedule_push();
  test_schedule_error_keeps_connection();
  test_wifi_setup_over_usb();
  if (g_failures == 0) std::printf("test_link: ok\n");
  return g_failures;
}

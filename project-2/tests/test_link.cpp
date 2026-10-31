// Link against a fake device on a pty: the fake runs the real rma2::Device.
#include <fcntl.h>
#include <poll.h>
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
#include "rma2/device.hpp"

using namespace std::chrono_literals;
using rma::Clock;

namespace {

class FakeDevice {
 public:
  FakeDevice() {
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
  ~FakeDevice() {
    done_ = true;
    thread_.join();
    close(master_);
    close(slave_);
  }
  const std::string& path() const { return path_; }
  void set_silent(bool s) { silent_ = s; }
  void set_knob_raw(uint16_t raw) { knob_raw_ = raw; }

  rma2::Settings settings() {
    std::lock_guard<std::mutex> lock(mu_);
    return dev_.settings();
  }
  bool playing() {
    std::lock_guard<std::mutex> lock(mu_);
    return dev_.playing();
  }
  int count(const std::string& line) {
    std::lock_guard<std::mutex> lock(mu_);
    int n = 0;
    for (auto& l : lines_) n += (l == line);
    return n;
  }

 private:
  static uint32_t now_ms() {
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count());
  }

  void run() {
    std::string cur;
    while (!done_) {
      pollfd p{master_, POLLIN, 0};
      ::poll(&p, 1, 5);
      char buf[256];
      ssize_t n = ::read(master_, buf, sizeof buf);
      std::lock_guard<std::mutex> lock(mu_);
      dev_.knob_sample(knob_raw_);
      dev_.tick(now_ms(), true);
      for (ssize_t i = 0; i < n; ++i) {
        char c = buf[i];
        if (c == '\n') {
          lines_.push_back(cur);
          cur.clear();
        } else if (c != '\r') {
          cur += c;
        }
        if (silent_) continue;
        char reply[rma2::Device::kMaxReply];
        if (dev_.feed(c, now_ms(), reply)) {
          std::string out = std::string(reply) + "\r\n";
          (void)!::write(master_, out.data(), out.size());
        }
      }
    }
  }

  int master_ = -1, slave_ = -1;
  std::string path_;
  std::thread thread_;
  std::atomic<bool> done_{false}, silent_{false};
  std::atomic<uint16_t> knob_raw_{0};
  std::mutex mu_;
  rma2::Device dev_;
  std::vector<std::string> lines_;
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

bool connected(rma2::Link& l) { return l.snapshot().state == rma2::LinkState::Connected; }

void test_settings_sound_and_knob() {
  FakeDevice fake;
  rma2::Link link([&] { return std::vector<std::string>{fake.path()}; });
  link.start();
  CHECK(eventually([&] { return connected(link); }, 2s));
  CHECK_STR(link.snapshot().fw, "0.2.0");

  rma2::Desired d;
  d.light = true;
  d.r = 10; d.g = 200; d.b = 30;
  d.sound = 2;
  d.meter = 3;
  link.set_desired(d);
  CHECK(eventually([&] {
    rma2::Settings s = fake.settings();
    return s.light && s.r == 10 && s.g == 200 && s.b == 30 && s.sound == 2 && s.meter == 3;
  }, 300ms));

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
  CHECK(fake.count("STOP") >= 2);
  CHECK(!fake.settings().light);  // quit turns the light off
}

void test_reconnect_never_plays_stale_clicks() {
  FakeDevice fake;
  std::atomic<bool> plugged{false};
  rma2::Link link([&] { return plugged ? std::vector<std::string>{fake.path()} : std::vector<std::string>{}; });
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
  FakeDevice fake;
  rma2::Link link([&] { return std::vector<std::string>{fake.path()}; });
  link.start();
  CHECK(eventually([&] { return connected(link); }, 2s));
  fake.set_silent(true);
  CHECK(eventually([&] { return !connected(link); }, 1500ms));
}

}  // namespace

int main() {
  test_settings_sound_and_knob();
  test_reconnect_never_plays_stale_clicks();
  test_silent_device_drops_connection();
  if (g_failures == 0) std::printf("test_link: ok\n");
  return g_failures;
}

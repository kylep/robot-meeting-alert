// Link against a fake device on a pty: the fake runs the real rma::Device.
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

using namespace std::chrono_literals;
using rma::Clock;

namespace {

enum class Mode { Normal, Silent, BadHello };

class FakeDevice {
 public:
  explicit FakeDevice(Mode mode = Mode::Normal) : mode_(mode) {
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
  void set_mode(Mode m) { mode_ = m; }

  rma::Outputs outputs() {
    std::lock_guard<std::mutex> lock(mu_);
    return dev_.outputs();
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
        std::chrono::duration_cast<std::chrono::milliseconds>(
            Clock::now().time_since_epoch())
            .count());
  }

  void run() {
    std::string cur;
    while (!done_) {
      pollfd p{master_, POLLIN, 0};
      ::poll(&p, 1, 5);
      char buf[256];
      ssize_t n = ::read(master_, buf, sizeof buf);
      std::lock_guard<std::mutex> lock(mu_);
      dev_.tick(now_ms(), true);
      for (ssize_t i = 0; i < n; ++i) {
        char c = buf[i];
        if (c == '\n') {
          lines_.push_back(cur);
          cur.clear();
        } else if (c != '\r') {
          cur += c;
        }
        if (mode_ == Mode::Silent) continue;
        char reply[rma::Device::kMaxReply];
        if (dev_.feed(c, now_ms(), reply)) {
          std::string out = reply;
          if (mode_ == Mode::BadHello && out.rfind("RMA ", 0) == 0) out = "ERR BADCMD";
          out += "\r\n";
          (void)!::write(master_, out.data(), out.size());
        }
      }
    }
  }

  std::atomic<Mode> mode_;
  int master_ = -1, slave_ = -1;
  std::string path_;
  std::thread thread_;
  std::atomic<bool> done_{false};
  std::mutex mu_;
  rma::Device dev_;
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

rma::Outputs O(bool r, bool g, bool b, bool z) {
  rma::Outputs o;
  o.r = r; o.g = g; o.b = b; o.buzz = z;
  return o;
}

bool same(const rma::Outputs& a, const rma::Outputs& b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.buzz == b.buzz;
}

void test_connect_and_sync() {
  FakeDevice fake;
  rma::Link link([&] { return std::vector<std::string>{fake.path()}; });
  link.start();
  CHECK(eventually([&] { return link.snapshot().state == rma::LinkState::Connected; }, 2s));
  CHECK_STR(link.snapshot().fw, "0.1.0");
  CHECK(fake.count("ALLOFF") >= 1);

  link.set_desired(O(1, 0, 1, 0));
  CHECK(eventually([&] { return same(fake.outputs(), O(1, 0, 1, 0)); }, 200ms));
  CHECK(eventually([&] { return same(link.snapshot().reported, O(1, 0, 1, 0)); }, 200ms));

  // Hold the buzzer like a GUI would: refresh every ~16 ms "frame". Give it
  // 150 ms to come on, then require it to stay on for 600 ms. If this thread
  // itself stalls past the Link's freshness window (slow CI runners do), a
  // release is the correct behaviour, so the stay-on check is skipped.
  auto hold = [&](Clock::duration d, bool* stayed_on, Clock::duration* worst_gap) {
    auto end = Clock::now() + d;
    auto last = Clock::now();
    while (Clock::now() < end) {
      link.set_desired(O(1, 0, 1, 1));
      auto now = Clock::now();
      if (now - last > *worst_gap) *worst_gap = now - last;
      last = now;
      if (stayed_on && !fake.outputs().buzz) *stayed_on = false;
      std::this_thread::sleep_for(16ms);
    }
  };
  Clock::duration warmup_gap{};
  hold(150ms, nullptr, &warmup_gap);
  CHECK(fake.outputs().buzz);
  bool always_on = true;
  Clock::duration worst_gap{};
  hold(600ms, &always_on, &worst_gap);
  if (worst_gap < rma::Link::kBuzzFreshness - 40ms && warmup_gap < rma::Link::kBuzzFreshness - 40ms) {
    CHECK(always_on);
  } else {
    std::printf("test_link: hold check skipped (test thread stalled %lld ms)\n",
                static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::max(worst_gap, warmup_gap)).count()));
  }
  CHECK(fake.count("BUZZ 1") >= 4);

  // Release.
  link.set_desired(O(1, 0, 1, 0));
  CHECK(eventually([&] { return !fake.outputs().buzz; }, 200ms));

  link.stop();
  CHECK(fake.outputs().r == false && fake.outputs().b == false);  // ALLOFF on stop
  CHECK(fake.count("ALLOFF") >= 2);
}

void test_gui_stall_releases_buzzer() {
  FakeDevice fake;
  rma::Link link([&] { return std::vector<std::string>{fake.path()}; });
  link.start();
  CHECK(eventually([&] { return link.snapshot().state == rma::LinkState::Connected; }, 2s));
  link.set_desired(O(0, 0, 0, 1));  // pressed once, then the GUI "hangs"
  CHECK(eventually([&] { return fake.outputs().buzz; }, 200ms));
  CHECK(eventually([&] { return !fake.outputs().buzz; }, 500ms));
  std::this_thread::sleep_for(400ms);
  CHECK(!fake.outputs().buzz);  // and it stays off
}

void test_silent_device_drops_connection() {
  FakeDevice fake;
  rma::Link link([&] { return std::vector<std::string>{fake.path()}; });
  link.start();
  CHECK(eventually([&] { return link.snapshot().state == rma::LinkState::Connected; }, 2s));
  fake.set_mode(Mode::Silent);
  CHECK(eventually([&] { return link.snapshot().state == rma::LinkState::Searching; }, 1500ms));
}

void test_bad_hello_never_connects() {
  FakeDevice fake(Mode::BadHello);
  rma::Link link([&] { return std::vector<std::string>{fake.path()}; });
  link.start();
  std::this_thread::sleep_for(1500ms);
  CHECK(link.snapshot().state == rma::LinkState::Searching);
}

void test_no_candidates() {
  rma::Link link([] { return std::vector<std::string>{}; });
  link.start();
  std::this_thread::sleep_for(50ms);
  CHECK(link.snapshot().state == rma::LinkState::Searching);
  auto t0 = Clock::now();
  link.stop();
  CHECK(Clock::now() - t0 < 200ms);  // stop is prompt even mid-wait
}

}  // namespace

int main() {
  test_connect_and_sync();
  test_gui_stall_releases_buzzer();
  test_silent_device_drops_connection();
  test_bad_hello_never_connects();
  test_no_candidates();
  if (g_failures == 0) std::printf("test_link: ok\n");
  return g_failures;
}

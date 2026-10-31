// rmactl (project 2): drive the Pico from the terminal.
//   rmactl [--port PATH] hello | state | light on|off | rgb R G B | sound N
//          | play [N] | stop | meter none|bright|vol|both | bal R G B | watch SECONDS
// `play` waits until the sound finishes (closing the port would stop it).
// Exit: 0 ok, 1 device error/timeout, 2 no device, 3 port busy, 64 usage.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "discovery.hpp"
#include "rma2/host.hpp"
#include "serial_port.hpp"

using namespace std::chrono_literals;
using rma::Clock;
using rma::IoResult;

namespace {

int usage() {
  std::fprintf(stderr,
               "usage: rmactl [--port PATH] <hello | state | light on|off | rgb R G B |\n"
               "              sound N | play [N] | stop | meter none|bright|vol|both |\n"
               "              bal R G B | watch SECONDS>\n");
  return 64;
}

// Sends one command; returns the exit code and the reply line.
int send_command(rma::SerialPort& port, const std::string& cmd, std::string* reply) {
  auto deadline = Clock::now() + 500ms;
  if (port.write_all(cmd + "\n", deadline) != IoResult::Ok) {
    std::fprintf(stderr, "rmactl: write failed\n");
    return 1;
  }
  std::string line;
  do {
    if (port.read_line(&line, deadline) != IoResult::Ok) {
      std::fprintf(stderr, "rmactl: no reply to %s\n", cmd.c_str());
      return 1;
    }
  } while (line.empty());
  *reply = line;
  return line.rfind("ERR", 0) == 0 ? 1 : 0;
}

bool is_num(const std::string& s) {
  return !s.empty() && s.size() <= 3 && s.find_first_not_of("0123456789") == std::string::npos;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> a(argv + 1, argv + argc);
  std::string port_path;
  if (a.size() >= 2 && a[0] == "--port") {
    port_path = a[1];
    a.erase(a.begin(), a.begin() + 2);
  }
  if (a.empty()) return usage();

  std::string cmd;
  int watch_s = 0;
  bool wait_for_sound = false;
  const std::string& v = a[0];
  if ((v == "hello" || v == "state" || v == "stop") && a.size() == 1) {
    cmd = v == "hello" ? "HELLO" : v == "state" ? "STATE" : "STOP";
  } else if (v == "light" && a.size() == 2 && (a[1] == "on" || a[1] == "off")) {
    cmd = a[1] == "on" ? "LIGHT 1" : "LIGHT 0";
  } else if ((v == "rgb" || v == "bal") && a.size() == 4 && is_num(a[1]) && is_num(a[2]) && is_num(a[3])) {
    cmd = (v == "rgb" ? "RGB " : "BAL ") + a[1] + " " + a[2] + " " + a[3];
  } else if (v == "sound" && a.size() == 2 && is_num(a[1])) {
    cmd = "SOUND " + a[1];
  } else if (v == "play" && (a.size() == 1 || (a.size() == 2 && is_num(a[1])))) {
    cmd = a.size() == 1 ? "PLAY" : "PLAY " + a[1];
    wait_for_sound = true;
  } else if (v == "meter" && a.size() == 2) {
    const char* modes[] = {"none", "bright", "vol", "both"};
    for (int i = 0; i < 4; ++i) {
      if (a[1] == modes[i]) cmd = "METER " + std::to_string(i);
    }
    if (cmd.empty()) return usage();
  } else if (v == "watch" && a.size() == 2 && is_num(a[1])) {
    watch_s = std::atoi(a[1].c_str());
    cmd = "STATE";
  } else {
    return usage();
  }

  std::vector<std::string> candidates = port_path.empty() ? rma::find_pico_ports() : std::vector<std::string>{port_path};
  if (candidates.empty()) {
    std::fprintf(stderr, "rmactl: no Raspberry Pi Pico found on USB\n");
    return 2;
  }
  rma::SerialPort port;
  switch (port.open(candidates.front())) {
    case rma::OpenResult::Ok:
      break;
    case rma::OpenResult::Busy:
      std::fprintf(stderr, "rmactl: %s is busy (quit Robot Meeting Alert first)\n", candidates.front().c_str());
      return 3;
    default:
      std::fprintf(stderr, "rmactl: cannot open %s\n", candidates.front().c_str());
      return 2;
  }

  std::string reply;
  if (int rc = send_command(port, cmd, &reply)) {
    if (!reply.empty()) std::printf("%s\n", reply.c_str());
    return rc;
  }
  std::printf("%s\n", reply.c_str());

  if (wait_for_sound) {  // keep the port open until the sound ends
    auto end = Clock::now() + 4500ms;
    rma2::State s;
    while (Clock::now() < end && rma2::parse_state(reply, &s) && s.playing) {
      std::this_thread::sleep_for(100ms);
      if (int rc = send_command(port, "STATE", &reply)) return rc;
    }
    std::printf("%s\n", reply.c_str());
  }
  if (watch_s > 0) {  // print the knob whenever it changes
    auto end = Clock::now() + std::chrono::seconds(watch_s);
    int last = -1;
    while (Clock::now() < end) {
      rma2::State s;
      if (int rc = send_command(port, "STATE", &reply)) return rc;
      if (rma2::parse_state(reply, &s) && s.knob != last) {
        std::printf("knob %3d%%\n", s.knob);
        std::fflush(stdout);
        last = s.knob;
      }
      std::this_thread::sleep_for(100ms);
    }
  }
  return 0;
}

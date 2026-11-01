// rmactl (project 3): drive the robot from the terminal, over USB or Wi-Fi.
//   rmactl [--port PATH | --host IP] hello | state | light on|off | rgb R G B
//          | sound N | play [N] | stop | meter none|bright|vol|both | bal R G B
//          | watch SECONDS | mode cal|manual | net | wifi | wificlr
//          | event IN_S DUR_S
// `play` waits until the sound finishes (closing the port would stop it).
// `wifi` asks for the network name and password on the terminal (USB only),
// so the password never lands on a command line or in shell history.
// `event` gives the robot one pretend meeting starting IN_S seconds from now.
// Exit: 0 ok, 1 device error/timeout, 2 no device, 3 port busy, 64 usage.
#include <termios.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "discovery.hpp"
#include "rma3/host.hpp"
#include "serial_port.hpp"

using namespace std::chrono_literals;
using rma::Clock;
using rma::IoResult;

namespace {

int usage() {
  std::fprintf(stderr,
               "usage: rmactl [--port PATH | --host IP] <hello | state | light on|off |\n"
               "              rgb R G B | sound N | play [N] | stop |\n"
               "              meter none|bright|vol|both | bal R G B | watch SECONDS |\n"
               "              mode cal|manual | net | wifi | wificlr | event IN_S DUR_S>\n");
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
      std::fprintf(stderr, "rmactl: no reply to %s\n", cmd.rfind("PASS", 0) == 0 ? "PASS" : cmd.c_str());
      return 1;
    }
  } while (line.empty());
  *reply = line;
  return line.rfind("ERR", 0) == 0 ? 1 : 0;
}

bool is_num(const std::string& s, size_t digits = 3) {
  return !s.empty() && s.size() <= digits && s.find_first_not_of("0123456789") == std::string::npos;
}

// Reads a line from the terminal, optionally without echo (passwords).
std::string prompt(const char* label, bool hidden) {
  std::fprintf(stderr, "%s", label);
  termios old{};
  bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old) == 0;
  if (hidden && tty) {
    termios quiet = old;
    quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &quiet);
  }
  std::string line;
  std::getline(std::cin, line);
  if (hidden && tty) {
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    std::fprintf(stderr, "\n");
  }
  return line;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> a(argv + 1, argv + argc);
  std::string port_path, host;
  if (a.size() >= 2 && a[0] == "--port") {
    port_path = a[1];
    a.erase(a.begin(), a.begin() + 2);
  } else if (a.size() >= 2 && a[0] == "--host") {
    host = a[1];
    a.erase(a.begin(), a.begin() + 2);
  }
  if (a.empty()) return usage();

  std::string cmd;
  std::vector<std::string> more;  // follow-up commands, sent in order
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
  } else if (v == "mode" && a.size() == 2 && (a[1] == "cal" || a[1] == "manual")) {
    cmd = a[1] == "cal" ? "MODE 1" : "MODE 0";
  } else if (v == "net" && a.size() == 1) {
    cmd = "NET";
  } else if (v == "wificlr" && a.size() == 1) {
    cmd = "WIFICLR";
  } else if ((v == "wifi" || v == "wificlr") && !host.empty()) {
    std::fprintf(stderr, "rmactl: %s works over USB only (the password must not cross the network)\n", v.c_str());
    return 64;
  } else if (v == "wifi" && a.size() == 1) {
    std::string ssid = prompt("Wi-Fi name: ", false);
    std::string pass = prompt("Wi-Fi password (empty for an open network): ", true);
    cmd = "WIFI " + ssid;
    more = {"PASS " + pass, "WIFIGO"};
  } else if (v == "event" && a.size() == 3 && is_num(a[1], 5) && is_num(a[2], 5) && std::atoi(a[2].c_str()) > 0) {
    int in = std::atoi(a[1].c_str()), dur = std::atoi(a[2].c_str());
    cmd = "CAL";
    more = {"EV 1 " + std::to_string(in) + " " + std::to_string(in + dur), "CALGO 3600", "MODE 1"};
  } else if (v == "watch" && a.size() == 2 && is_num(a[1])) {
    watch_s = std::atoi(a[1].c_str());
    cmd = "STATE";
  } else {
    return usage();
  }

  rma::SerialPort port;
  if (!host.empty()) {
    if (port.open_tcp(host, rma::kRobotPort, 2s) != rma::OpenResult::Ok) {
      std::fprintf(stderr, "rmactl: no robot answering at %s:%d\n", host.c_str(), rma::kRobotPort);
      return 2;
    }
  }
  std::vector<std::string> candidates = port_path.empty() ? rma::find_pico_ports() : std::vector<std::string>{port_path};
  if (host.empty() && candidates.empty()) {
    std::fprintf(stderr, "rmactl: no Raspberry Pi Pico found on USB (use --host IP for Wi-Fi)\n");
    return 2;
  }
  switch (host.empty() ? port.open(candidates.front()) : rma::OpenResult::Ok) {
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
  for (const std::string& m : more) {
    if (int rc = send_command(port, m, &reply)) {
      std::printf("%s\n", reply.c_str());
      return rc;
    }
  }
  bool secret = cmd.rfind("WIFI ", 0) == 0;
  if (!secret || more.empty()) std::printf("%s\n", reply.c_str());
  else std::printf("saved; the robot is joining the network (check with: rmactl net)\n");

  if (wait_for_sound) {  // keep the port open until the sound ends
    auto end = Clock::now() + 4500ms;
    rma3::State s;
    while (Clock::now() < end && rma3::parse_state(reply, &s) && s.playing) {
      std::this_thread::sleep_for(100ms);
      if (int rc = send_command(port, "STATE", &reply)) return rc;
    }
    std::printf("%s\n", reply.c_str());
  }
  if (watch_s > 0) {  // print the knob whenever it changes
    auto end = Clock::now() + std::chrono::seconds(watch_s);
    int last = -1;
    while (Clock::now() < end) {
      rma3::State s;
      if (int rc = send_command(port, "STATE", &reply)) return rc;
      if (rma3::parse_state(reply, &s) && s.knob != last) {
        std::printf("knob %3d%%\n", s.knob);
        std::fflush(stdout);
        last = s.knob;
      }
      std::this_thread::sleep_for(100ms);
    }
  }
  return 0;
}

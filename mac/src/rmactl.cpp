// rmactl: drive the Pico from the terminal (hardware smoke tests, agents).
//   rmactl [--port PATH] hello|state|alloff|led r|g|b on|off|buzz MS
// Exit: 0 ok, 1 device error/timeout, 2 no device, 3 port busy, 64 usage.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "discovery.hpp"
#include "rma/host.hpp"
#include "serial_port.hpp"

using namespace std::chrono_literals;
using rma::Clock;
using rma::IoResult;

namespace {

int usage() {
  std::fprintf(stderr,
               "usage: rmactl [--port PATH] <hello | state | alloff |\n"
               "              led r|g|b on|off | buzz MS>\n");
  return 64;
}

// Sends one command and prints its reply. Returns the exit code.
int send_command(rma::SerialPort& port, const std::string& cmd, bool quiet = false) {
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
  if (!quiet) std::printf("%s\n", line.c_str());
  return line.rfind("ERR", 0) == 0 ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> args(argv + 1, argv + argc);
  std::string port_path;
  if (args.size() >= 2 && args[0] == "--port") {
    port_path = args[1];
    args.erase(args.begin(), args.begin() + 2);
  }
  if (args.empty()) return usage();

  std::vector<std::string> cmds;
  int buzz_ms = 0;
  const std::string& verb = args[0];
  if (verb == "hello" && args.size() == 1) {
    cmds = {"HELLO"};
  } else if (verb == "state" && args.size() == 1) {
    cmds = {"STATE"};
  } else if (verb == "alloff" && args.size() == 1) {
    cmds = {"ALLOFF"};
  } else if (verb == "led" && args.size() == 3 &&
             (args[1] == "r" || args[1] == "g" || args[1] == "b") &&
             (args[2] == "on" || args[2] == "off")) {
    char c = static_cast<char>(args[1][0] - 'a' + 'A');
    cmds = {std::string("LED ") + c + (args[2] == "on" ? " 1" : " 0")};
  } else if (verb == "buzz" && args.size() == 2) {
    buzz_ms = std::atoi(args[1].c_str());
    if (buzz_ms <= 0 || buzz_ms > 10000) return usage();
  } else {
    return usage();
  }

  std::vector<std::string> candidates;
  if (!port_path.empty()) {
    candidates = {port_path};
  } else {
    candidates = rma::find_pico_ports();
  }
  if (candidates.empty()) {
    std::fprintf(stderr, "rmactl: no Raspberry Pi Pico found on USB\n");
    return 2;
  }

  rma::SerialPort port;
  switch (port.open(candidates.front())) {
    case rma::OpenResult::Ok:
      break;
    case rma::OpenResult::Busy:
      std::fprintf(stderr, "rmactl: %s is busy (quit Robot Meeting Alert first)\n",
                   candidates.front().c_str());
      return 3;
    default:
      std::fprintf(stderr, "rmactl: cannot open %s\n", candidates.front().c_str());
      return 2;
  }

  if (buzz_ms > 0) {
    auto end = Clock::now() + std::chrono::milliseconds(buzz_ms);
    while (Clock::now() < end) {
      if (int rc = send_command(port, "BUZZ 1", true)) return rc;
      std::this_thread::sleep_for(100ms);
    }
    return send_command(port, "BUZZ 0");
  }
  return send_command(port, cmds.front());
}

#include "discovery.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <sys/syslimits.h>

#include <algorithm>
#include <chrono>

namespace rma {
namespace {

int usb_vendor_id(io_object_t service) {
  CFTypeRef prop = IORegistryEntrySearchCFProperty(
      service, kIOServicePlane, CFSTR("idVendor"), kCFAllocatorDefault,
      kIORegistryIterateRecursively | kIORegistryIterateParents);
  int vid = 0;
  if (prop != nullptr) {
    if (CFGetTypeID(prop) == CFNumberGetTypeID()) {
      CFNumberGetValue(static_cast<CFNumberRef>(prop), kCFNumberIntType, &vid);
    }
    CFRelease(prop);
  }
  return vid;
}

bool callout_path(io_object_t service, std::string* out) {
  CFTypeRef prop = IORegistryEntryCreateCFProperty(
      service, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
  if (prop == nullptr) return false;
  char buf[PATH_MAX];
  bool ok = CFGetTypeID(prop) == CFStringGetTypeID() &&
            CFStringGetCString(static_cast<CFStringRef>(prop), buf, sizeof buf,
                               kCFStringEncodingUTF8);
  CFRelease(prop);
  if (ok) *out = buf;
  return ok;
}

}  // namespace

std::vector<std::string> find_pico_ports() {
  std::vector<std::string> ports;
  io_iterator_t it = 0;
  // IOServiceGetMatchingServices consumes the matching dictionary.
  if (IOServiceGetMatchingServices(kIOMainPortDefault,
                                   IOServiceMatching(kIOSerialBSDServiceValue),
                                   &it) != KERN_SUCCESS) {
    return ports;
  }
  while (io_object_t service = IOIteratorNext(it)) {
    std::string path;
    if (usb_vendor_id(service) == kRaspberryPiVendorId && callout_path(service, &path)) {
      ports.push_back(path);
    }
    IOObjectRelease(service);
  }
  IOObjectRelease(it);
  std::sort(ports.begin(), ports.end());
  return ports;
}

}  // namespace rma

// ------------------------------------------------------------ Wi-Fi finder

namespace rma {

RobotFinder::RobotFinder() : worker_([this] { run(); }) {}

RobotFinder::~RobotFinder() {
  stop_ = true;
  if (worker_.joinable()) worker_.join();
}

void RobotFinder::set_manual(const std::string& ip) {
  std::lock_guard<std::mutex> lock(mu_);
  manual_ = ip;
}

std::string RobotFinder::found() const {
  std::lock_guard<std::mutex> lock(mu_);
  return found_;
}

std::vector<std::string> RobotFinder::candidates() const {
  std::vector<std::string> out = find_pico_ports();
  std::lock_guard<std::mutex> lock(mu_);
  for (const std::string& ip : {manual_, found_}) {
    if (ip.empty()) continue;
    std::string c = "tcp:" + ip + ":" + std::to_string(kRobotPort);
    bool dup = false;
    for (const auto& o : out) dup = dup || o == c;
    if (!dup) out.push_back(c);
  }
  return out;
}

void RobotFinder::run() {
  using namespace std::chrono_literals;
  while (!stop_) {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo("rma.local", nullptr, &hints, &res) == 0 && res) {
      char buf[INET_ADDRSTRLEN] = {};
      auto* sin = reinterpret_cast<sockaddr_in*>(res->ai_addr);
      if (inet_ntop(AF_INET, &sin->sin_addr, buf, sizeof buf)) {
        std::lock_guard<std::mutex> lock(mu_);
        found_ = buf;  // keep the last good one if a later lookup fails
      }
      freeaddrinfo(res);
    }
    for (int i = 0; i < 50 && !stop_; ++i) std::this_thread::sleep_for(100ms);
  }
}

}  // namespace rma

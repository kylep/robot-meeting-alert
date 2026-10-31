#include "discovery.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <sys/syslimits.h>

#include <algorithm>

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

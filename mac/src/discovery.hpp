#pragma once

#include <string>
#include <vector>

namespace rma {

constexpr int kRaspberryPiVendorId = 0x2E8A;

// Lists /dev/cu.* callout devices whose USB ancestor has the Raspberry Pi
// vendor ID. Unrelated serial devices are never returned, so never opened.
std::vector<std::string> find_pico_ports();

}  // namespace rma

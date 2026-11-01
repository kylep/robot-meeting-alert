// This Mac's copy of the robot key, made from the robot passphrase (never
// the passphrase itself): 16 bytes in
// ~/Library/Application Support/RobotMeetingAlert/pairing.key (mode 0600),
// shared by the app and rmactl. Any Mac where the same passphrase is typed
// gets the same key and can control the robot over Wi-Fi. Setting or changing
// the passphrase on the robot needs USB.
#pragma once

#include <cstdint>
#include <string>

namespace rma {

std::string pairing_key_path();
bool load_pairing_key(uint8_t key[16]);  // false if no passphrase saved yet
bool save_pairing_key(const uint8_t key[16]);

}  // namespace rma

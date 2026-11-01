// This Mac's pairing key for the robot: 16 random bytes in
// ~/Library/Application Support/RobotMeetingAlert/pairing.key (mode 0600),
// shared by the app and rmactl. Plugging the robot in with USB while the app
// runs gives the robot this key; over Wi-Fi the robot then only obeys a Mac
// that can prove it holds the same key.
#pragma once

#include <cstdint>
#include <string>

namespace rma {

std::string pairing_key_path();
// Loads the key, creating a new random one if there is none yet.
bool load_or_create_pairing_key(uint8_t key[16]);
// Loads only (rmactl): false if this Mac was never paired.
bool load_pairing_key(uint8_t key[16]);

}  // namespace rma

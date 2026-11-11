// This Mac's copy of the robot key, made from the robot passphrase (never
// the passphrase itself): 16 bytes in
// ~/Library/Application Support/RobotMeetingAlert/pairing.key (mode 0600),
// shared by the app and rmactl. Any Mac where the same passphrase is typed
// gets the same key and can control the robot over Wi-Fi. Setting or changing
// the passphrase on the robot needs USB.
//
// The Windows build (../project-3b) implements the same calls with
// %APPDATA%\RobotMeetingAlert and a user-only file ACL.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

// How the app and rmactl refer to the computer they run on.
#ifdef _WIN32
#define RMA_THIS_COMPUTER "this PC"
#define RMA_COMPUTER "PC"
#else
#define RMA_THIS_COMPUTER "this Mac"
#define RMA_COMPUTER "Mac"
#endif

namespace rma {

// The folder for the app's settings and secrets (created when saving).
std::filesystem::path app_data_dir();

std::string pairing_key_path();
bool load_pairing_key(uint8_t key[16]);  // false if no passphrase saved yet
bool save_pairing_key(const uint8_t key[16]);

// The "iCal link" calendar source's address (or .ics file path). Such a link
// usually lets anyone read the calendar, so it is kept like the key: in the
// same folder, readable only by this user, and never logged. "" = none;
// saving "" deletes it.
std::string load_ics_link();
bool save_ics_link(const std::string& link);

}  // namespace rma

// Host-side protocol helpers (macOS app, CLI, tests). Not linked into firmware.
#pragma once

#include <string>
#include <string_view>

#include "device.hpp"

namespace rma {

std::string format_state(const Outputs& o);  // "R1 G0 B0 Z0"

// Accepts exactly "OK R<b> G<b> B<b> Z<b>".
bool parse_state_reply(std::string_view line, Outputs* out);

// Accepts "RMA 1 <version>" (protocol major 1 only).
bool parse_hello_reply(std::string_view line, std::string* fw_version);

}  // namespace rma

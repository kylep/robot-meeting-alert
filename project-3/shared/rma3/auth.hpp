// Pairing: the Mac and the robot share a 16-byte key (set over USB). Over
// Wi-Fi the robot sends a fresh random challenge and the Mac answers with
// HMAC-SHA256(key, challenge), so the key itself never crosses the network.
// Plain C++ (no SDK, no heap) so the robot, the Mac and the tests share it.
#pragma once

#include <cstddef>
#include <cstdint>

namespace rma3 {

constexpr size_t kKeyLen = 16;
constexpr size_t kNonceLen = 16;
constexpr size_t kProofLen = 16;  // first 16 bytes of the HMAC

void sha256(const uint8_t* data, size_t len, uint8_t out[32]);
void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len, uint8_t out[32]);

// proof = HMAC-SHA256(key, nonce), truncated.
void auth_proof(const uint8_t key[kKeyLen], const uint8_t nonce[kNonceLen], uint8_t out[kProofLen]);

// Compares without an early exit (timing doesn't reveal how much matched).
bool same_bytes(const uint8_t* a, const uint8_t* b, size_t n);

// Lowercase hex. to_hex writes 2n chars + NUL; from_hex needs exactly 2n hex chars.
void to_hex(const uint8_t* in, size_t n, char* out);
bool from_hex(const char* in, uint8_t* out, size_t n);

}  // namespace rma3

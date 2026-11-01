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

// The handshake over Wi-Fi (both sides hold the key; it never crosses the network):
//   Mac   -> HI <nm>                  nm: the Mac's random nonce
//   robot -> OK HI <nr> <robot_proof>  nr: the robot's random nonce
//   Mac checks robot_proof first (a fake robot can't produce it), then
//   Mac   -> AUTH <mac_proof>
// After that every Mac line ends in " #<tag>": a MAC of the line under a
// session key, with a counter, so lines can't be forged, changed or replayed.
// Each value is HMAC-SHA256(key, label || nm || nr), labels keep them apart.
constexpr size_t kTagLen = 8;
void robot_proof(const uint8_t key[kKeyLen], const uint8_t nm[kNonceLen], const uint8_t nr[kNonceLen],
                 uint8_t out[kProofLen]);
void mac_proof(const uint8_t key[kKeyLen], const uint8_t nm[kNonceLen], const uint8_t nr[kNonceLen],
               uint8_t out[kProofLen]);
void session_key(const uint8_t key[kKeyLen], const uint8_t nm[kNonceLen], const uint8_t nr[kNonceLen],
                 uint8_t out[kKeyLen]);
// tag = HMAC-SHA256(session key, counter (4 bytes, big-endian) || line), truncated.
void line_tag(const uint8_t ks[kKeyLen], uint32_t counter, const char* line, size_t len, uint8_t out[kTagLen]);

// PBKDF2-HMAC-SHA256 (RFC 8018), out_len <= 32 (one block is all we need).
void pbkdf2_sha256(const char* pass, size_t pass_len, const uint8_t* salt, size_t salt_len, uint32_t iters,
                   uint8_t* out, size_t out_len);
// The pairing key from a passphrase (Mac side only; the robot stores the key).
// Deliberately slow (200,000 rounds) so guessing passphrases offline is slow too.
constexpr uint32_t kPassphraseRounds = 200000;
void passphrase_key(const char* passphrase, uint8_t out[kKeyLen]);

// Compares without an early exit (timing doesn't reveal how much matched).
bool same_bytes(const uint8_t* a, const uint8_t* b, size_t n);

// Lowercase hex. to_hex writes 2n chars + NUL; from_hex needs exactly 2n hex chars.
void to_hex(const uint8_t* in, size_t n, char* out);
bool from_hex(const char* in, uint8_t* out, size_t n);

}  // namespace rma3

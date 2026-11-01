#include "auth.hpp"

#include <cstring>

namespace rma3 {
namespace {

constexpr uint32_t kK[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

struct Sha256 {
  uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                   0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  uint8_t block[64] = {};
  size_t fill = 0;
  uint64_t total = 0;

  void compress() {
    uint32_t w[64];
    for (int i = 0; i < 16; ++i) {
      w[i] = (uint32_t(block[4 * i]) << 24) | (uint32_t(block[4 * i + 1]) << 16) |
             (uint32_t(block[4 * i + 2]) << 8) | block[4 * i + 3];
    }
    for (int i = 16; i < 64; ++i) {
      uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
      uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], k = h[7];
    for (int i = 0; i < 64; ++i) {
      uint32_t t1 = k + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + kK[i] + w[i];
      uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
      k = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += k;
  }
  void update(const uint8_t* p, size_t n) {
    total += n;
    while (n--) {
      block[fill++] = *p++;
      if (fill == 64) {
        compress();
        fill = 0;
      }
    }
  }
  void finish(uint8_t out[32]) {
    uint64_t bits = total * 8;
    uint8_t pad = 0x80;
    update(&pad, 1);
    uint8_t zero = 0;
    while (fill != 56) update(&zero, 1);
    for (int i = 7; i >= 0; --i) {
      uint8_t byte = static_cast<uint8_t>(bits >> (8 * i));
      update(&byte, 1);
    }
    for (int i = 0; i < 8; ++i) {
      out[4 * i] = static_cast<uint8_t>(h[i] >> 24);
      out[4 * i + 1] = static_cast<uint8_t>(h[i] >> 16);
      out[4 * i + 2] = static_cast<uint8_t>(h[i] >> 8);
      out[4 * i + 3] = static_cast<uint8_t>(h[i]);
    }
  }
};

}  // namespace

void sha256(const uint8_t* data, size_t len, uint8_t out[32]) {
  Sha256 s;
  s.update(data, len);
  s.finish(out);
}

void hmac_sha256(const uint8_t* key, size_t key_len, const uint8_t* msg, size_t msg_len, uint8_t out[32]) {
  uint8_t k[64] = {};
  if (key_len > 64) {
    sha256(key, key_len, k);
  } else {
    std::memcpy(k, key, key_len);
  }
  uint8_t ipad[64], opad[64];
  for (int i = 0; i < 64; ++i) {
    ipad[i] = k[i] ^ 0x36;
    opad[i] = k[i] ^ 0x5c;
  }
  uint8_t inner[32];
  Sha256 a;
  a.update(ipad, 64);
  a.update(msg, msg_len);
  a.finish(inner);
  Sha256 b;
  b.update(opad, 64);
  b.update(inner, 32);
  b.finish(out);
}

namespace {

void labelled(const uint8_t key[kKeyLen], const char* label, const uint8_t nm[kNonceLen],
              const uint8_t nr[kNonceLen], uint8_t* out, size_t out_len) {
  uint8_t msg[16 + 2 * kNonceLen];
  size_t l = std::strlen(label);
  std::memcpy(msg, label, l);
  std::memcpy(msg + l, nm, kNonceLen);
  std::memcpy(msg + l + kNonceLen, nr, kNonceLen);
  uint8_t mac[32];
  hmac_sha256(key, kKeyLen, msg, l + 2 * kNonceLen, mac);
  std::memcpy(out, mac, out_len);
}

}  // namespace

void robot_proof(const uint8_t key[kKeyLen], const uint8_t nm[kNonceLen], const uint8_t nr[kNonceLen],
                 uint8_t out[kProofLen]) {
  labelled(key, "rma3 robot", nm, nr, out, kProofLen);
}

void mac_proof(const uint8_t key[kKeyLen], const uint8_t nm[kNonceLen], const uint8_t nr[kNonceLen],
               uint8_t out[kProofLen]) {
  labelled(key, "rma3 mac", nm, nr, out, kProofLen);
}

void session_key(const uint8_t key[kKeyLen], const uint8_t nm[kNonceLen], const uint8_t nr[kNonceLen],
                 uint8_t out[kKeyLen]) {
  labelled(key, "rma3 session", nm, nr, out, kKeyLen);
}

void line_tag(const uint8_t ks[kKeyLen], uint32_t counter, const char* line, size_t len, uint8_t out[kTagLen]) {
  uint8_t msg[4 + 128];
  if (len > 128) len = 128;  // lines are at most 96 bytes
  msg[0] = static_cast<uint8_t>(counter >> 24);
  msg[1] = static_cast<uint8_t>(counter >> 16);
  msg[2] = static_cast<uint8_t>(counter >> 8);
  msg[3] = static_cast<uint8_t>(counter);
  std::memcpy(msg + 4, line, len);
  uint8_t mac[32];
  hmac_sha256(ks, kKeyLen, msg, 4 + len, mac);
  std::memcpy(out, mac, kTagLen);
}

void pbkdf2_sha256(const char* pass, size_t pass_len, const uint8_t* salt, size_t salt_len, uint32_t iters,
                   uint8_t* out, size_t out_len) {
  // Block 1 only: U1 = HMAC(P, S || 00000001), Ui = HMAC(P, Ui-1), T = U1 ^ ... ^ Uc.
  uint8_t msg[128];
  if (salt_len > sizeof msg - 4) salt_len = sizeof msg - 4;
  std::memcpy(msg, salt, salt_len);
  msg[salt_len] = 0; msg[salt_len + 1] = 0; msg[salt_len + 2] = 0; msg[salt_len + 3] = 1;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(pass);
  uint8_t u[32], t[32];
  hmac_sha256(p, pass_len, msg, salt_len + 4, u);
  std::memcpy(t, u, 32);
  for (uint32_t i = 1; i < iters; ++i) {
    hmac_sha256(p, pass_len, u, 32, u);
    for (int k = 0; k < 32; ++k) t[k] ^= u[k];
  }
  std::memcpy(out, t, out_len > 32 ? 32 : out_len);
}

void passphrase_key(const char* passphrase, uint8_t out[kKeyLen]) {
  static const char kSalt[] = "robot-meeting-alert pairing v1";
  pbkdf2_sha256(passphrase, std::strlen(passphrase), reinterpret_cast<const uint8_t*>(kSalt), sizeof kSalt - 1,
                kPassphraseRounds, out, kKeyLen);
}

bool same_bytes(const uint8_t* a, const uint8_t* b, size_t n) {
  uint8_t diff = 0;
  for (size_t i = 0; i < n; ++i) diff |= a[i] ^ b[i];
  return diff == 0;
}

void to_hex(const uint8_t* in, size_t n, char* out) {
  static const char kHex[] = "0123456789abcdef";
  for (size_t i = 0; i < n; ++i) {
    out[2 * i] = kHex[in[i] >> 4];
    out[2 * i + 1] = kHex[in[i] & 15];
  }
  out[2 * n] = '\0';
}

bool from_hex(const char* in, uint8_t* out, size_t n) {
  auto val = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  };
  if (std::strlen(in) != 2 * n) return false;
  for (size_t i = 0; i < n; ++i) {
    int hi = val(in[2 * i]), lo = val(in[2 * i + 1]);
    if (hi < 0 || lo < 0) return false;
    out[i] = static_cast<uint8_t>(hi * 16 + lo);
  }
  return true;
}

}  // namespace rma3

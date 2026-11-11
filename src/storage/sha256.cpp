#include <algorithm>
#include <cstring>
#include "internal.h"

namespace storage::detail
{
namespace
{
constexpr uint32_t constants[] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
uint32_t rotate(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32 - bits));
}
}  // namespace

void Sha256::block()
{
    uint32_t words[64];
    for (size_t i = 0; i < 16; ++i)
        words[i] = uint32_t(buffer[i * 4]) << 24 | uint32_t(buffer[i * 4 + 1]) << 16 | uint32_t(buffer[i * 4 + 2]) << 8 | buffer[i * 4 + 3];
    for (size_t i = 16; i < 64; ++i) {
        auto a = words[i - 15], b = words[i - 2];
        words[i] = words[i - 16] + (rotate(a, 7) ^ rotate(a, 18) ^ (a >> 3)) + words[i - 7] + (rotate(b, 17) ^ rotate(b, 19) ^ (b >> 10));
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6], h = state[7];
    for (size_t i = 0; i < 64; ++i) {
        auto first = h + (rotate(e, 6) ^ rotate(e, 11) ^ rotate(e, 25)) + ((e & f) ^ (~e & g)) + constants[i] + words[i];
        auto second = (rotate(a, 2) ^ rotate(a, 13) ^ rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + first;
        d = c;
        c = b;
        b = a;
        a = first + second;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void Sha256::append(std::string_view bytes)
{
    length += bytes.size();
    while (!bytes.empty()) {
        auto count = std::min(bytes.size(), sizeof(buffer) - used);
        std::memcpy(buffer + used, bytes.data(), count);
        used += count;
        bytes.remove_prefix(count);
        if (used == sizeof(buffer)) {
            block();
            used = 0;
        }
    }
}

Digest Sha256::finish()
{
    auto bits = length * 8;
    buffer[used++] = 0x80;
    if (used > 56) {
        std::fill(buffer + used, buffer + 64, 0);
        block();
        used = 0;
    }
    std::fill(buffer + used, buffer + 56, 0);
    for (unsigned i = 0; i < 8; ++i)
        buffer[63 - i] = uint8_t(bits >> (i * 8));
    block();
    Digest result{};
    for (size_t i = 0; i < result.size(); ++i)
        result[i] = uint8_t(state[i / 4] >> (24 - 8 * (i % 4)));
    return result;
}
}  // namespace storage::detail

#include "util/hash.h"

#include <cstring>
#include <string>
#include <string_view>

namespace pmake::util {

namespace {

inline std::uint32_t rol(std::uint32_t v, std::uint32_t n) {
    return (v << n) | (v >> (32 - n));
}

// SHA1. Реализация по RFC 3174. Публичный домен-совместимая.
std::array<std::uint8_t, 20> sha1_raw(std::string_view data) {
    std::uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89,
                  h2 = 0x98BADCFE, h3 = 0x10325476,
                  h4 = 0xC3D2E1F0;

    // Padding
    std::string padded(data);
    std::uint64_t bitLen = static_cast<std::uint64_t>(padded.size()) * 8;
    padded.push_back(static_cast<char>(0x80));
    while ((padded.size() % 64) != 56) padded.push_back('\0');
    for (int i = 7; i >= 0; --i)
        padded.push_back(static_cast<char>((bitLen >> (i * 8)) & 0xFF));

    for (std::size_t chunk = 0; chunk < padded.size(); chunk += 64) {
        std::uint32_t w[80];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint8_t>(padded[chunk + i*4 + 0]) << 24)
                 | (static_cast<std::uint8_t>(padded[chunk + i*4 + 1]) << 16)
                 | (static_cast<std::uint8_t>(padded[chunk + i*4 + 2]) << 8)
                 | (static_cast<std::uint8_t>(padded[chunk + i*4 + 3]));
        }
        for (int i = 16; i < 80; ++i)
            w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if      (i < 20) { f = (b & c) | (~b & d);          k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
            else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }
            std::uint32_t tmp = rol(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol(b, 30); b = a; a = tmp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    std::array<std::uint8_t, 20> out{};
    auto put = [&](int off, std::uint32_t v) {
        out[off+0] = (v >> 24) & 0xFF;
        out[off+1] = (v >> 16) & 0xFF;
        out[off+2] = (v >> 8)  & 0xFF;
        out[off+3] = v         & 0xFF;
    };
    put(0, h0); put(4, h1); put(8, h2); put(12, h3); put(16, h4);
    return out;
}

} // namespace

std::string sha1_hex(std::string_view data) {
    auto raw = sha1_raw(data);
    static const char hex[] = "0123456789abcdef";
    std::string out(40, '0');
    for (std::size_t i = 0; i < raw.size(); ++i) {
        out[i*2+0] = hex[raw[i] >> 4];
        out[i*2+1] = hex[raw[i] & 0xF];
    }
    return out;
}

} // namespace pmake::util
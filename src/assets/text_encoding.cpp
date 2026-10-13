#include "assets/text_encoding.hpp"

#include <array>
#include <cstdint>

namespace oc2::assets {

namespace {

// Коды Unicode для байтов 0x80–0xBF (0xC0–0xFF — А…я подряд, U+0410…U+044F).
constexpr std::array<std::uint16_t, 64> kHigh = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A,
    0x040C, 0x040B, 0x040F, 0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0xFFFD, 0x2122,
    0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F, 0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6,
    0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407, 0x00B0, 0x00B1, 0x0406, 0x0456,
    0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
};

void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(char(cp));
    } else if (cp < 0x800) {
        out.push_back(char(0xC0 | (cp >> 6)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(char(0xE0 | (cp >> 12)));
        out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(char(0x80 | (cp & 0x3F)));
    }
}

} // namespace

std::string cp1251ToUtf8(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 2);
    for (char ch : text) {
        const auto b = static_cast<unsigned char>(ch);
        if (b < 0x80)
            out.push_back(ch);
        else if (b < 0xC0)
            appendUtf8(out, kHigh[b - 0x80]);
        else
            appendUtf8(out, 0x0410u + (b - 0xC0u));
    }
    return out;
}

std::string utf8ToCp1251(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const auto b = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = 0;
        std::size_t len = 1;
        if (b < 0x80) {
            cp = b;
        } else if ((b & 0xE0) == 0xC0 && i + 1 < text.size()) {
            cp = ((b & 0x1Fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
            len = 2;
        } else if ((b & 0xF0) == 0xE0 && i + 2 < text.size()) {
            cp = ((b & 0x0Fu) << 12) | ((static_cast<unsigned char>(text[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(text[i + 2]) & 0x3Fu);
            len = 3;
        } else {
            cp = '?';
            len = (b & 0xF8) == 0xF0 ? 4 : 1;
        }
        i += len;

        if (cp < 0x80) {
            out.push_back(char(cp));
        } else if (cp >= 0x0410 && cp <= 0x044F) {
            out.push_back(char(0xC0 + (cp - 0x0410)));
        } else {
            char c = '?';
            for (std::size_t k = 0; k < kHigh.size(); ++k)
                if (kHigh[k] == cp) {
                    c = char(0x80 + k);
                    break;
                }
            out.push_back(c);
        }
    }
    return out;
}

} // namespace oc2::assets

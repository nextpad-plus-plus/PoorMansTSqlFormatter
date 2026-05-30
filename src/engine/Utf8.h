/*
 * Utf8.h — minimal UTF-8 ↔ code-point helpers.
 *
 * The C# library processes text as UTF-16 code units (System.Char). We process
 * Unicode code points (char32_t) so identifier bytes and the BMP currency-prefix
 * symbols (e.g. U+20AC) compare correctly — matching C# semantics for the BMP
 * (astral chars, absent from SQL, are the only divergence vs. UTF-16 units).
 */
#pragma once

#include <string>
#include <cstdint>

namespace pmsf {

// Decode a UTF-8 string into code points. Invalid bytes are passed through as
// their raw value (0x80–0xFF) so nothing is lost on malformed input.
inline std::u32string utf8Decode(const std::string& s) {
    std::u32string out;
    out.reserve(s.size());
    size_t i = 0, n = s.size();
    while (i < n) {
        unsigned char c = (unsigned char)s[i];
        char32_t cp;
        int extra;
        if (c < 0x80)      { cp = c;          extra = 0; }
        else if (c < 0xE0) { cp = c & 0x1F;   extra = 1; }
        else if (c < 0xF0) { cp = c & 0x0F;   extra = 2; }
        else if (c < 0xF8) { cp = c & 0x07;   extra = 3; }
        else               { out.push_back(c); ++i; continue; }  // invalid lead byte
        if (i + extra >= n) { out.push_back(c); ++i; continue; } // truncated
        bool ok = true;
        for (int k = 1; k <= extra; ++k) {
            unsigned char cc = (unsigned char)s[i + k];
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok) { out.push_back(c); ++i; continue; }
        out.push_back(cp);
        i += extra + 1;
    }
    return out;
}

// Append one code point to a UTF-8 std::string.
inline void utf8Append(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out.push_back((char)cp);
    } else if (cp < 0x800) {
        out.push_back((char)(0xC0 | (cp >> 6)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back((char)(0xE0 | (cp >> 12)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    } else {
        out.push_back((char)(0xF0 | (cp >> 18)));
        out.push_back((char)(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back((char)(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back((char)(0x80 | (cp & 0x3F)));
    }
}

inline std::string utf8Encode(const std::u32string& s) {
    std::string out;
    out.reserve(s.size());
    for (char32_t cp : s) utf8Append(out, cp);
    return out;
}

} // namespace pmsf

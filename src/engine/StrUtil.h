/*
 * StrUtil.h — small string helpers mirroring the .NET String methods the
 * parser/formatter rely on (ToUpperInvariant, StartsWith, EndsWith, TrimEnd, …).
 * Keywords are ASCII, so ToUpperInvariant is implemented as ASCII upper (non-ASCII
 * identifier bytes are left unchanged, which never affects keyword matching).
 */
#pragma once

#include <string>
#include <cctype>

namespace pmsf {

inline std::string toUpperAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    return out;
}

inline std::string toLowerAscii(const std::string& s) {
    std::string out = s;
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return out;
}

// Replace every occurrence of `from` with `to` (mirrors .NET String.Replace).
inline std::string replaceAll(const std::string& s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    std::string out;
    size_t pos = 0, next;
    while ((next = s.find(from, pos)) != std::string::npos) {
        out.append(s, pos, next - pos);
        out += to;
        pos = next + from.size();
    }
    out.append(s, pos, std::string::npos);
    return out;
}

// True if the string contains any CR or LF (mirrors Regex.IsMatch(@"(\r|\n)+")).
inline bool containsLineBreak(const std::string& s) {
    return s.find_first_of("\r\n") != std::string::npos;
}

inline bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

inline bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

inline std::string trimEnd(const std::string& s) {
    size_t end = s.size();
    while (end > 0) {
        char c = s[end - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v') --end;
        else break;
    }
    return s.substr(0, end);
}

inline bool isNullOrEmpty(const std::string& s) { return s.empty(); }

} // namespace pmsf

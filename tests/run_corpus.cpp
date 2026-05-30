/*
 * run_corpus.cpp — exact-clone validation harness for the standard formatter.
 *
 * Iterates every tests/corpus/StandardFormatSql/<name>[(opts)].txt expectation,
 * formats tests/corpus/InputSql/<name>.txt with the encoded options, and does an
 * EXACT byte compare (after stripping the UTF-8 BOM that .NET ReadAllText drops).
 *
 * HTMLColoring=true expectations are skipped: the macOS plugin has no HTML mode
 * (matching the Windows "Format" command, which always runs HTMLColoring=false).
 */
#include "../src/engine/Engine.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

namespace fs = std::filesystem;
using namespace pmsf;

static std::string readFileStripBOM(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
        s.erase(0, 3);
    return s;
}

// Show the first differing line for diagnosis.
static void reportFirstDiff(const std::string& a, const std::string& b) {
    size_t i = 0, line = 1, col = 1;
    while (i < a.size() && i < b.size() && a[i] == b[i]) {
        if (a[i] == '\n') { ++line; col = 1; } else ++col;
        ++i;
    }
    auto vis = [](const std::string& s, size_t pos) {
        size_t start = pos, end = pos;
        while (start > 0 && s[start - 1] != '\n') --start;
        while (end < s.size() && s[end] != '\n') ++end;
        std::string seg = s.substr(start, end - start);
        std::string out;
        for (char c : seg) { if (c == '\r') out += "\\r"; else if (c == '\t') out += "\\t"; else out += c; }
        return out;
    };
    std::cerr << "    first diff at line " << line << ", col " << col
              << " (byte " << i << ")\n";
    std::cerr << "    expected: [" << vis(b, i < b.size() ? i : b.size()) << "]\n";
    std::cerr << "    actual  : [" << vis(a, i < a.size() ? i : a.size()) << "]\n";
    std::cerr << "    lengths: actual=" << a.size() << " expected=" << b.size() << "\n";
}

int main(int argc, char** argv) {
    fs::path corpus = (argc > 1) ? fs::path(argv[1])
                                 : fs::path(__FILE__).parent_path() / "corpus";
    fs::path inputDir = corpus / "InputSql";
    fs::path expectDir = corpus / "StandardFormatSql";

    std::vector<fs::path> files;
    for (auto& e : fs::directory_iterator(expectDir))
        if (e.is_regular_file() && e.path().extension() == ".txt")
            files.push_back(e.path());
    std::sort(files.begin(), files.end());

    int passed = 0, failed = 0, skipped = 0;
    std::vector<std::string> failures;

    for (const auto& expectPath : files) {
        std::string stem = expectPath.stem().string();   // filename without .txt
        std::string namePart = stem, optionString;
        auto paren = stem.find('(');
        if (paren != std::string::npos) {
            namePart = stem.substr(0, paren);
            auto close = stem.rfind(')');
            optionString = stem.substr(paren + 1, close - paren - 1);
        }

        if (optionString.find("HTMLColoring=true") != std::string::npos
            || optionString.find("HTMLColoring=True") != std::string::npos) {
            std::cout << "SKIP (HTML) " << stem << "\n";
            ++skipped;
            continue;
        }

        fs::path inputPath = inputDir / (namePart + ".txt");
        if (!fs::exists(inputPath)) {
            std::cerr << "MISSING INPUT for " << stem << " (expected " << inputPath << ")\n";
            ++failed; failures.push_back(stem + " [missing input]");
            continue;
        }

        std::string input = readFileStripBOM(inputPath);
        std::string expected = readFileStripBOM(expectPath);

        std::string actual;
        try {
            TSqlStandardFormatterOptions opts(optionString);
            actual = formatSql(input, opts);
        } catch (const std::exception& ex) {
            std::cerr << "FAIL  " << stem << "  (exception: " << ex.what() << ")\n";
            ++failed; failures.push_back(stem + " [exception]");
            continue;
        }

        if (actual == expected) {
            ++passed;
            // std::cout << "PASS  " << stem << "\n";
        } else {
            std::cerr << "FAIL  " << stem << "\n";
            reportFirstDiff(actual, expected);
            ++failed; failures.push_back(stem);
        }
    }

    std::cout << "\n==== corpus: " << passed << " passed, " << failed
              << " failed, " << skipped << " skipped ====\n";
    if (!failures.empty()) {
        std::cout << "failures:\n";
        for (auto& f : failures) std::cout << "  - " << f << "\n";
    }
    return failed == 0 ? 0 : 1;
}

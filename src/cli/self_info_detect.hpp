#pragma once

#include <string>
#include <array>
#include <cctype>

namespace icmg::cli {

// Pure heuristic: fraction of "self-referential" signal vs "work" signal in text.
// Returns s/(s+w) where s = count of SELF-word substring hits, w = count of
// WORK-word substring hits (both case-insensitive). 0.0 when neither appears.
inline double looksLikeSelfInfo(const std::string& text) {
    std::string lower(text);
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    static const std::array<const char*, 15> kSelfWords = {{
        "i feel", "my feeling", "feeling", "identity", "who i am",
        "my name", "persona", "relationship", "aku merasa", "perasaan",
        "identitas", "jati diri", "kepribadian", "hubungan", "esensi"
    }};
    static const std::array<const char*, 13> kWorkWords = {{
        "bug", "build", "commit", "ctest", "compile", "function",
        "class ", "error", "migration", "refactor", "cmake", "linker", "file"
    }};

    auto countHits = [&lower](const char* needle) -> int {
        int hits = 0;
        std::string n(needle);
        if (n.empty()) return 0;
        std::string::size_type pos = 0;
        while ((pos = lower.find(n, pos)) != std::string::npos) {
            ++hits;
            pos += n.size();
        }
        return hits;
    };

    int s = 0, w = 0;
    for (const char* word : kSelfWords) s += countHits(word);
    for (const char* word : kWorkWords) w += countHits(word);

    if (s + w == 0) return 0.0;
    return static_cast<double>(s) / static_cast<double>(s + w);
}

} // namespace icmg::cli

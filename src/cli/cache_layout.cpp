#include "cache_layout.hpp"
#include <cstdint>
#include <algorithm>
#include <cctype>
#include <sstream>

namespace icmg::cli {

std::string fnv1aHex(const std::string& s) {
    // FNV-1a 64-bit.
    std::uint64_t h = 1469598103934665603ULL;      // offset basis
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;                      // FNV prime
    }
    static const char* hex = "0123456789abcdef";
    std::string out(16, '0');
    for (int i = 15; i >= 0; --i) { out[i] = hex[h & 0xF]; h >>= 4; }
    return out;
}

CacheLayout assembleCacheAware(const std::vector<ContextSegment>& segs,
                               const CacheEmitOptions& opts) {
    CacheLayout L;

    // Collect stable and volatile bodies in input order, skipping empties so a
    // blank section can't perturb the byte-stable prefix.
    std::string stable, tail;
    for (const auto& s : segs) {
        if (s.body.empty()) continue;
        if (s.vol == Volatility::Stable) {
            if (!stable.empty()) stable += "\n";
            stable += s.body;
            ++L.stable_count;
        } else {
            if (!tail.empty()) tail += "\n";
            tail += s.body;
            ++L.volatile_count;
        }
    }

    L.prefix_bytes = stable.size();
    L.tail_bytes   = tail.size();

    if (!stable.empty()) {
        // Hash the raw stable prefix BEFORE wrapping, so the fingerprint tracks
        // the content a caller controls (sentinel ttl noise excluded).
        L.prefix_hash = fnv1aHex(stable);
        L.text        = wrapCachePrefix(stable, opts);
        L.wrapped     = true;
        if (!tail.empty()) L.text += "\n" + tail;
    } else {
        // No cacheable prefix -> emit the tail verbatim, no sentinel.
        L.text = tail;
    }
    return L;
}

std::vector<ContextSegment> classifyMarkdownSections(const std::string& md) {
    // Header substrings (lowercased) that mark a STABLE, cache-worthy section.
    static const char* kStable[] = {
        "convention", "rule", "files & symbols", "files and symbols",
        "graph", "architecture", "reference template", "pinned", "skeleton",
    };
    auto lower = [](std::string s) {
        for (char& c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    };
    auto isHeader = [](const std::string& line) {
        // "# " or "## " (up to 3 hashes) then text.
        size_t i = 0; while (i < line.size() && line[i] == '#') ++i;
        return i >= 1 && i <= 3 && i < line.size() && line[i] == ' ';
    };
    auto classify = [&](const std::string& header) {
        std::string h = lower(header);
        for (const char* s : kStable)
            if (h.find(s) != std::string::npos) return Volatility::Stable;
        return Volatility::Volatile;
    };

    std::vector<ContextSegment> segs;
    std::istringstream in(md);
    std::string line;
    std::string curHeader, curBody;
    bool haveSection = false;

    auto flush = [&]() {
        if (!haveSection) return;
        std::string body = curHeader;
        if (!curBody.empty()) { body += "\n"; body += curBody; }
        ContextSegment seg;
        seg.label = curHeader;
        seg.body  = body;
        seg.vol   = classify(curHeader);
        segs.push_back(std::move(seg));
    };

    while (std::getline(in, line)) {
        if (isHeader(line)) {
            flush();
            curHeader = line;
            curBody.clear();
            haveSection = true;
        } else if (haveSection) {
            if (!curBody.empty()) curBody += "\n";
            curBody += line;
        } else {
            // Pre-header preamble (before any header) -> its own volatile segment.
            if (!line.empty()) {
                if (segs.empty() || !segs.front().label.empty()) {
                    ContextSegment pre; pre.label = ""; pre.body = line;
                    pre.vol = Volatility::Volatile;
                    segs.insert(segs.begin(), std::move(pre));
                } else {
                    segs.front().body += "\n" + line;
                }
            }
        }
    }
    flush();
    return segs;
}

} // namespace icmg::cli

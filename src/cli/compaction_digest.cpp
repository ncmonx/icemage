#include "compaction_digest.hpp"
#include "last_session.hpp"
#include "../core/utf8_prefix.hpp"
#include <sstream>

namespace icmg::cli {

namespace {
constexpr const char* kEllipsis = "...";
constexpr size_t      kEllipsisBytes = 3;
}  // namespace

size_t sentenceEnd(const std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '\n') return i;
        if ((c == '.' || c == '!' || c == '?') &&
            (i + 1 == s.size() || s[i + 1] == ' ' || s[i + 1] == '\t' ||
             s[i + 1] == '\r' || s[i + 1] == '\n'))
            return i + 1;
    }
    return std::string::npos;
}

std::vector<imem::MemoryNode> compactionInputs(const std::vector<imem::MemoryNode>& hits,
                                               size_t limit) {
    std::vector<imem::MemoryNode> out;
    for (const auto& h : hits) {
        if (out.size() >= limit) break;
        if (isSessionTopic(h.topic)) continue;
        out.push_back(h);
    }
    return out;
}

std::string extractiveDigest(const std::vector<imem::MemoryNode>& hits, int maxLines,
                             size_t maxSentenceBytes) {
    std::ostringstream o;
    int n = 0;
    for (const auto& h : hits) {
        if (n >= maxLines) break;
        std::string c = h.content;
        size_t stop = sentenceEnd(c);
        if (stop != std::string::npos) c = c.substr(0, stop);
        while (!c.empty() && (c.back() == '.' || c.back() == '\r')) c.pop_back();
        if (c.size() > maxSentenceBytes) {
            size_t keep = maxSentenceBytes > kEllipsisBytes ? maxSentenceBytes - kEllipsisBytes : 0;
            c = core::utf8Prefix(c, keep) + kEllipsis;
        }
        o << "- " << h.topic << ": " << c << " [#" << h.id << "]\n";
        ++n;
    }
    return o.str();
}

std::string sourceIdsLine(const std::vector<imem::MemoryNode>& hits) {
    if (hits.empty()) return "";
    std::ostringstream o;
    o << "src:";
    for (const auto& h : hits) o << " #" << h.id;
    o << "\n";
    return o.str();
}

}  // namespace icmg::cli

// 2026-09-23: graph-coherence pull for salience compression -- IDE-E from the
// landscape scan (TopoCompress, arXiv 2608.30811). Per-line salience scoring
// is structure-blind: it keeps the line citing dispatcher.cpp but drops the
// line citing registry.hpp even though the two files are WIRED in the code
// graph -- the kept evidence loses its structural context. Deterministic
// repair: pull back dropped lines whose cited file is graph-adjacent to a
// file cited by a kept line. Same shape as danglingRepairLines (2608.04569);
// reuses the file-ref extractor from the grounding scan. Pure ops, no DB --
// the cmd layer feeds basename-level adjacency from graph_edges.
#pragma once
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../imem/grounding_scan.hpp"   // extractFileRefs + toLower

namespace icmg::core {

// Lowercased basenames of files cited by `line` ("src/cli/x.cpp" -> "x.cpp").
inline std::set<std::string> citedBasenames(const std::string& line) {
    std::set<std::string> out;
    for (const auto& ref : imem::extractFileRefs(line)) {
        const size_t slash = ref.rfind('/');
        out.insert(imem::grounding_detail::toLower(
            slash == std::string::npos ? ref : ref.substr(slash + 1)));
    }
    return out;
}

// Indices of DROPPED lines to pull back: their cited file is adjacent (in the
// code graph) to a file cited by a KEPT line. Capped at max_pulls -- coherence
// repair must not quietly undo the compression it is repairing.
inline std::vector<size_t> graphCoherencePulls(
    const std::vector<std::string>& lines,
    const std::vector<bool>& keep,
    const std::map<std::string, std::set<std::string>>& adjacency,
    size_t max_pulls = 8) {
    std::vector<size_t> pulls;
    if (adjacency.empty() || lines.size() != keep.size()) return pulls;

    // Files cited by kept lines + everything adjacent to them = wanted set.
    std::set<std::string> wanted;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (!keep[i]) continue;
        for (const auto& base : citedBasenames(lines[i])) {
            auto it = adjacency.find(base);
            if (it == adjacency.end()) continue;
            for (const auto& adj : it->second) wanted.insert(adj);
        }
    }
    if (wanted.empty()) return pulls;

    for (size_t i = 0; i < lines.size() && pulls.size() < max_pulls; ++i) {
        if (keep[i]) continue;
        for (const auto& base : citedBasenames(lines[i])) {
            if (wanted.count(base)) { pulls.push_back(i); break; }
        }
    }
    return pulls;
}

}  // namespace icmg::core

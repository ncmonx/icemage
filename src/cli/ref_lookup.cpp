#include "ref_lookup.hpp"

#include "../core/db.hpp"
#include "../core/path_u8.hpp"
#include "../imem/ai_ref_store.hpp"

#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace icmg::cli::refflow {

namespace {
constexpr size_t kRefLineCap = 160;   // chars of problem/solution per injected line
}

RefSection modelAiRefSection(core::Db& ref_db, const std::string& prompt, int limit,
                             const std::function<bool(const std::string&)>& already_injected,
                             const std::function<void(const std::string&)>& mark_injected) {
    RefSection sec;
    try {
        imem::AiRefStore store(ref_db);
        std::ostringstream out;
        for (const auto& r : store.search(prompt, limit * 2)) {
            if (sec.hits >= limit) break;
            const double cov = refCoverage(prompt, r.problem);
            if (cov < kRefMinCoverage) continue;
            const std::string key = "ref:" + std::to_string(r.id);
            if (already_injected && already_injected(key)) continue;
            if (sec.best_score < cov) sec.best_score = cov;
            out << "  [" << key << "][" << (int)(cov * 100) << "%]"
                << (r.project.empty() ? "" : " (" + r.project + ")") << " "
                << capUtf8(collapseWs(r.problem), kRefLineCap) << "\n"
                << "    -> " << capUtf8(collapseWs(r.solution), kRefLineCap) << "\n";
            if (mark_injected) mark_injected(key);
            store.markUsed(r.id);
            ++sec.hits;
        }
        if (sec.hits > 0)
            sec.text = "icmg Model-AI refs (solved before, any project):\n" + out.str() + "\n";
    } catch (...) {
        sec = RefSection{};
    }
    return sec;
}

std::vector<ProjectEntry> shareableProjects(core::Db& global_db,
                                            const std::string& home,
                                            const std::string& cwd) {
    std::vector<ProjectEntry> out;
    try {
        global_db.query("SELECT name, path, db_path FROM projects ORDER BY id", {},
                        [&](const core::Row& r) {
                            if (r.size() < 3) return;
                            if (!isShareableProject(r[1], home, cwd)) return;
                            ProjectEntry e{r[0], r[1], r[2]};
                            if (e.db_path.empty())
                                e.db_path = core::pathU8(core::u8ToPath(e.path) / ".icmg" / "data.db");
                            out.push_back(e);
                        });
    } catch (...) {}
    return out;
}

}  // namespace icmg::cli::refflow

#pragma once
// v2.30.0 Model-AI ref flow: problem -> solution references kept in the
// "Model AI" DB (the exe-dir icmg-persona.db, see core/persona_db.hpp), shared
// by every project on this install. Written back automatically only for
// verified work (cli/ref_flow.hpp decideWriteback), read by the prompt hook
// before the project-B/C fallback and by `icmg recall --refs`.
// Spec: docs/plans/2026-11-13-model-ai-ref-flow.md
//
// No migration runner exists for the persona DB, so the table is created
// lazily (ensure()), same as ProfileStore.

#include <cstdint>
#include <string>
#include <vector>

namespace icmg::core { class Db; }

namespace icmg::imem {

struct AiRefInput {
    std::string problem;
    std::string solution;
    std::string project;      // where the ref came from (name or path)
    std::string source_ref;   // e.g. memory id / commit, optional
    std::string verified_by;  // verify command that passed, optional
};

struct AiRef {
    long long   id = 0;
    std::string problem;
    std::string solution;
    std::string project;
    std::string source_ref;
    std::string verified_by;
    long long   created_at = 0;
    int         used_count = 0;
    long long   last_used  = 0;
    double      score      = 0.0;   // search only: higher = better (-bm25)
};

class AiRefStore {
public:
    explicit AiRefStore(core::Db& db);

    // Insert a ref; returns its id. A near-duplicate problem returns the
    // existing id (no new row). Empty problem -> 0.
    long long add(const AiRefInput& in);

    // Best-first refs for `query` (FTS5 bm25 over problem+solution; LIKE
    // fallback when FTS5 is unavailable). Empty query -> empty.
    std::vector<AiRef> search(const std::string& query, int limit);

    // True when a stored problem is a near-duplicate (word Jaccard >=
    // refflow::kRefDedupJaccard) of `problem`.
    bool isDuplicate(const std::string& problem);

    bool get(long long id, AiRef& out);
    void markUsed(long long id);
    // Delete a ref (stale/noisy auto write-back). False when id is unknown.
    bool remove(long long id);
    int  count();

private:
    void ensure();
    long long findDuplicate(const std::string& problem);
    core::Db& db_;
    bool fts_ = false;
};

// The "Model AI" DB: exe-dir icmg-persona.db when writable, else the global
// DB (same fallback rule as core::writePersona).
core::Db& modelAiRefDb();

}  // namespace icmg::imem

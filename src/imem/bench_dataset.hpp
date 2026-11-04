// 2026-10-29: standard AI-memory benchmark loaders (Tier 1, retrieval-only).
//
// LongMemEval (Wu et al., ICLR 2025; longmemeval_s / _m / _oracle .json):
//   one instance = one question + its own haystack of chat sessions. Gold
//   evidence = answer_session_ids (session level) or turns with has_answer
//   (turn level). question_id ending in "_abs" = abstention (no evidence).
// LoCoMo (Maharana et al., ACL 2024; locomo10.json):
//   one sample = one long two-speaker conversation + many QA. Gold evidence =
//   dia_id list ("D<session>:<turn>"). category 5 = adversarial/unanswerable.
//
// Both are mapped to the same neutral BenchCorpus so the runner + metrics are
// dataset-agnostic. Pure parsing, no DB, no LLM.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace icmg::imem {

// UserSession = one doc per session holding only the user's turns (the unit
// rtk-ai/icm reports LongMemEval with). LongMemEval only.
enum class BenchGranularity { Session, Turn, UserSession };

// "session" | "turn" | "user-session" -> g. false on an unknown name.
bool parseBenchGranularity(const std::string& name, BenchGranularity& g);

struct BenchDoc {
    std::string id;     // evidence key (session id / turn id)
    std::string text;   // what gets stored as one memory node
    int64_t     date = 0;  // UTC epoch of the session (0 = unknown)
};

struct BenchQuestion {
    std::string id;
    std::string category;
    std::string question;
    std::vector<std::string> evidence;  // gold doc ids
    int64_t     as_of = 0;              // recall sees only docs dated <= as_of
    bool        abstention = false;     // unanswerable by design
};

struct BenchCorpus {
    std::vector<BenchDoc>      docs;
    std::vector<BenchQuestion> questions;
};

// Calls fn(objectText) for every top-level {...} in a JSON array text, without
// building a DOM for the whole file (LongMemEval-S is ~270MB). String- and
// escape-aware. Returns the number of objects visited.
int forEachTopLevelObject(const std::string& text,
                          const std::function<void(const std::string&)>& fn);

// "2023/05/20 (Sat) 02:21" (LongMemEval) or "1:56 pm on 8 May, 2023"
// (LoCoMo) -> UTC epoch seconds. 0 when unparseable.
int64_t parseBenchDate(const std::string& s);

// One LongMemEval instance object -> corpus with exactly one question.
BenchCorpus parseLongMemEval(const std::string& instanceJson, BenchGranularity g);

// One LoCoMo sample object -> corpus with all of its QA.
BenchCorpus parseLoCoMo(const std::string& sampleJson, BenchGranularity g);

}  // namespace icmg::imem

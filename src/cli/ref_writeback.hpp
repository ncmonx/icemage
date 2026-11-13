#pragma once
// v2.30.0 Model-AI ref flow, write-back half ("Simpan Result / Ref").
// At `hook stop`, scan the transcript of the finished turn and store a
// problem -> solution ref in the Model-AI DB (and project A memory) only when
// the work changed files AND a test/build command passed (decideWriteback).
// Spec: docs/plans/2026-11-13-model-ai-ref-flow.md sec 4.3

#include "ref_flow.hpp"

#include <string>
#include <vector>

namespace icmg::core { class Db; }

namespace icmg::cli::refflow {

constexpr size_t kRefMinProblemLen      = 20;            // shorter prompts ("lanjut") are follow-ups
constexpr size_t kRefTranscriptTailBytes = 4 * 1024 * 1024; // only the tail of a big transcript is read
constexpr size_t kRefOutcomeCap         = 200;
constexpr const char* kRefProjectTopic  = "refs-auto";   // project-A memory topic for write-backs

// What one finished turn did, from the transcript JSONL.
struct TurnScan {
    std::string problem;                 // last substantive user prompt
    std::vector<std::string> edited_files;  // Edit/Write/MultiEdit/NotebookEdit since the last prompt
    bool verified_ok = false;            // a test/build Bash command succeeded
    bool verify_failed_after = false;    // the LAST such command failed
    std::string verify_cmd;              // last successful verify command
    std::string outcome;                 // first line of the last assistant text
};

bool isVerifyCommand(const std::string& cmd);
TurnScan scanTranscriptTurn(const std::string& jsonl);

struct WritebackContext {
    std::string transcript_path;
    std::string project;                 // label stored with the ref (project A name)
    core::Db*   ref_db = nullptr;        // Model-AI DB (required)
    core::Db*   project_db = nullptr;    // project A DB (optional: only when cwd is a project)
    bool        disabled = false;
    int         stored_this_session = 0;
};

// Scan + decide + write. Never throws.
WritebackDecision runRefWriteback(const WritebackContext& ctx);

// Hook entry: parses the Stop stdin JSON (transcript_path, cwd, session_id),
// opens the Model-AI DB and project A DB, keeps a per-session counter.
// Returns the decision name ("" when skipped before deciding).
std::string runRefWritebackHook(const std::string& stop_stdin);

}  // namespace icmg::cli::refflow

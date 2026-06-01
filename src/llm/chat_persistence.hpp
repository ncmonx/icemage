// v1.48.0: persistent local-LLM chat history in global.db.
// Storage is SEPARATE from project memory_nodes per user constraint.
// See migrations/global/0032_local_llm_chats.sql for schema.
#pragma once

#include <string>
#include <utility>
#include <vector>
#include <cstdint>

namespace icmg::llm {

struct ChatTurn {
    std::string role;     // "user" / "assistant" / "system"
    std::string content;
    std::int64_t ts = 0;
};

struct ChatSession {
    std::string session_id;
    std::int64_t ts = 0;
    std::string preview;       // first ~80 chars of first user msg
    int turn_count = 0;
};

struct ChatRecall {
    std::string session_id;
    std::int64_t ts = 0;
    std::string content;
    double score = 0.0;
};

// Append one turn. Idempotent on (user_id, session_id, ts, role) — same row
// pattern. Returns true on success.
bool appendChatTurn(const std::string& user_id,
                    const std::string& session_id,
                    const std::string& role,
                    const std::string& content,
                    const std::string& model_id = "",
                    int tokens_in = 0,
                    int tokens_out = 0);

// Load most recent turns for a session, ascending ts. Returns role+content
// pairs ready to feed into buildChatMLPromptMulti.
std::vector<std::pair<std::string, std::string>>
loadSessionHistory(const std::string& user_id,
                   const std::string& session_id,
                   int max_turns = 20);

// v1.48.0: load most recent turns ACROSS all sessions for user.
// Used when user wants chat history to flow continuously regardless
// of which icmg chat invocation. session_id boundary becomes loose.
std::vector<std::pair<std::string, std::string>>
loadRecentTurns(const std::string& user_id, int max_turns = 20);

// List recent sessions (most recent first). Each session = first user msg
// preview + turn count + ts.
std::vector<ChatSession>
listRecentSessions(const std::string& user_id, int limit = 20);

// BM25 recall over past chat content (FTS5). Returns top-K hits, scored.
// Cross-session, per-user. Used by personal-assistant context-injection.
std::vector<ChatRecall>
bm25RecallChats(const std::string& user_id,
                const std::string& query,
                int top_k = 3);

// Clear a session (DELETE rows). Used by `\new` slash cmd when user wants
// fresh start AND erase the session, not just start a new one.
bool clearSession(const std::string& user_id, const std::string& session_id);

} // namespace icmg::llm

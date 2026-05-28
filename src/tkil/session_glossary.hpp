// v1.56 T1 Stage 5: session glossary.
//
// Tracks per-line frequency within a single Claude Code session. Once a
// line crosses the threshold (default 3 occurrences), subsequent
// appearances are replaced by a short token ("$S<N>") so the AI sees the
// repeated phrase compactly. The full phrase is recoverable via expand().
//
// Per-instance state — production wiring (v1.56 T3 daemon) will hold one
// instance per session_id keyed by the hook session_id field. Tests
// instantiate a fresh glossary per case (no global singleton).

#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace icmg::tkil {

struct GlossaryOpts {
    int min_count = 3;         // occurrences required before tokenisation
    int min_phrase_len = 8;    // shorter lines not worth tokenising
};

class SessionGlossary {
public:
    SessionGlossary();
    explicit SessionGlossary(const GlossaryOpts& opts);

    // Process input, returning a possibly-tokenised view. Side effect:
    // updates internal counters and token map.
    std::string apply(const std::string& input);

    // Return original phrase for token (e.g. "$S1"). nullopt if unknown.
    std::optional<std::string> expand(const std::string& token) const;

    // Clear counters and token map (used between sessions).
    void reset();

private:
    GlossaryOpts opts_;
    std::unordered_map<std::string, int>         counts_;       // phrase -> count
    std::unordered_map<std::string, std::string> phrase_to_tok_;// phrase -> "$S<N>"
    std::unordered_map<std::string, std::string> tok_to_phrase_;// "$S<N>" -> phrase
    int next_token_id_ = 1;
};

}  // namespace icmg::tkil

// v1.56 T1 Stage 5: session glossary — implementation.

#include "session_glossary.hpp"
#include "dedup_pass.hpp"   // isAlwaysVerbatim

#include <sstream>
#include <string>

namespace icmg::tkil {

SessionGlossary::SessionGlossary() : opts_({}) {}

SessionGlossary::SessionGlossary(const GlossaryOpts& opts) : opts_(opts) {}

void SessionGlossary::reset() {
    counts_.clear();
    phrase_to_tok_.clear();
    tok_to_phrase_.clear();
    next_token_id_ = 1;
}

std::optional<std::string>
SessionGlossary::expand(const std::string& token) const {
    auto it = tok_to_phrase_.find(token);
    if (it == tok_to_phrase_.end()) return std::nullopt;
    return it->second;
}

std::string SessionGlossary::apply(const std::string& input) {
    if (input.empty()) return input;

    std::istringstream is(input);
    std::ostringstream os;
    std::string line;
    bool had_trailing_newline = !input.empty() && input.back() == '\n';

    while (std::getline(is, line)) {
        // Error / fatal lines never tokenised — surface real signal.
        if (isAlwaysVerbatim(line)) {
            os << line << '\n';
            continue;
        }
        // Skip too-short phrases (token cost ≥ phrase cost).
        if ((int)line.size() < opts_.min_phrase_len) {
            os << line << '\n';
            continue;
        }

        // Track count for this phrase.
        int& cnt = counts_[line];
        ++cnt;

        // Assign or reuse a token if at/over threshold.
        if (cnt >= opts_.min_count) {
            auto it = phrase_to_tok_.find(line);
            std::string tok;
            if (it == phrase_to_tok_.end()) {
                tok = "$S" + std::to_string(next_token_id_++);
                phrase_to_tok_[line] = tok;
                tok_to_phrase_[tok]  = line;
            } else {
                tok = it->second;
            }
            os << tok << '\n';
        } else {
            os << line << '\n';
        }
    }

    std::string result = os.str();
    if (!had_trailing_newline && !result.empty() && result.back() == '\n') {
        result.pop_back();
    }
    return result;
}

}  // namespace icmg::tkil

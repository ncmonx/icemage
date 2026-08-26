// 2026-06-07: pure helpers for `icmg store --quick` (no-topic frictionless capture).
// No DB/IO. quickTopic() makes a deterministic timestamped topic; firstPositional() picks the
// message arg, skipping flags and their values so `store --quick "msg"` needs no topic.
#pragma once
#include <string>
#include <vector>

namespace icmg::cli {

// Deterministic auto-topic for a quick note: "quick:<epoch>". Recall finds it by the "quick"
// prefix (topic-prefix convention). Epoch keeps it sortable + unique without localtime.
inline std::string quickTopic(long long epoch) {
    return "quick:" + std::to_string(epoch);
}

// First positional arg: not a flag (--x) and not the value following a value-taking flag.
// Lets `icmg store --quick "msg"` and `icmg store --kw a --quick "msg"` both find "msg".
// Returns "" if none.
inline std::string firstPositional(const std::vector<std::string>& args,
                                   const std::vector<std::string>& valueFlags) {
    auto isValueFlag = [&](const std::string& a){
        for (auto& f : valueFlags) if (a == f) return true;
        return false;
    };
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i].rfind("--", 0) == 0) continue;                 // a flag
        if (i > 0 && isValueFlag(args[i-1])) continue;             // value of a flag
        return args[i];
    }
    return "";
}

// 2026-08-25: topic/content resolution for `icmg store`, with `--topic T` compat.
// Canonical form is positional (`store <topic> <content>`), but years of emitted
// AGENTS.md templates taught `store --topic T "<text>"`; under pure positional
// parsing that stored topic="--topic", content=T and silently DISCARDED the
// text (real data loss). Honor both forms.
struct StoreArgs {
    std::string topic;
    std::string content;
};

inline StoreArgs resolveStoreArgs(const std::vector<std::string>& args) {
    StoreArgs r;
    // value-taking flags: their trailing value is never topic/content
    static const std::vector<std::string> vf =
        {"--kw", "--importance", "--ttl", "--source", "--topic"};
    // --topic compat form
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--topic") {
            if (i + 1 < args.size()) r.topic = args[i + 1];
            r.content = firstPositional(args, vf);
            return r;
        }
    }
    // canonical positional form
    if (args.size() >= 1) r.topic = args[0];
    if (args.size() >= 2) r.content = args[1];
    return r;
}

} // namespace icmg::cli

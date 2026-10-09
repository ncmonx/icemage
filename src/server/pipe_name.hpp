#pragma once
// Game-sidecar pilot: project-scoped daemon pipe names.
//
// One machine can host several icmg daemons (dev daemon + one per game
// server project) as long as each listens on its own pipe. The pipe name is
// resolved once in server_cmd and threaded through IcmgServer/PipeClient.
//
// Precedence: --pipe <name> flag  >  ICMG_SERVER_PIPE env  >  default.
// Names are validated flat (alnum, '-', '_', '.') — no path separators, so a
// crafted name cannot escape the \\.\pipe\ namespace or a unix socket dir.

#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace icmg::server {

inline constexpr const char* kDefaultPipeName = "icmg-server";

struct PipeNameResult {
    std::string              name;   // resolved pipe name
    std::vector<std::string> args;   // input args with --pipe <name> stripped
};

// True when 'name' is a safe flat pipe name.
inline bool isValidPipeName(const std::string& name) {
    if (name.empty() || name.size() > 128) return false;
    for (unsigned char c : name) {
        if (!(std::isalnum(c) || c == '-' || c == '_' || c == '.'))
            return false;
    }
    return true;
}

// Extract --pipe <name> from args (any position); fall back to
// ICMG_SERVER_PIPE env, then kDefaultPipeName. Invalid names fall back to
// the default rather than being passed through.
inline PipeNameResult resolvePipeName(const std::vector<std::string>& args) {
    PipeNameResult r;
    r.name = kDefaultPipeName;

    std::string flag_value;
    for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--pipe") {
            if (i + 1 < args.size()) {
                flag_value = args[i + 1];
                ++i;  // skip value
            }
            continue;  // strip flag (and value) from pass-through args
        }
        r.args.push_back(args[i]);
    }

    if (!flag_value.empty() && isValidPipeName(flag_value)) {
        r.name = flag_value;
        return r;
    }

    if (const char* env = std::getenv("ICMG_SERVER_PIPE"); env && *env) {
        std::string env_name(env);
        if (isValidPipeName(env_name)) r.name = env_name;
    }
    return r;
}

}  // namespace icmg::server

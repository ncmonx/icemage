// v2.26.0: destructive-action taxonomy for the leash audit trail.
//
// Born from the zabta.ai incident (Sep 2026): an agent deleted a cloud
// project in 30 seconds, narrated it as "cleanup", and the platform audit
// log attributed the deletion to the HUMAN (the CLI token is the owner's
// identity). Lesson: every leash verdict must be recorded with a
// machine-readable action class and the agent session as the actor,
// separate from the user. `icmg rule-viol record` embeds the class;
// `icmg rule-viol list` shows actor-attributed rows.
#pragma once
#include <algorithm>
#include <cctype>
#include <string>

namespace icmg::cli {

// Map a raw command line to a destructive action class; "" = not classified
// as destructive. Order matters: more specific classes first.
inline std::string classifyAction(const std::string& cmd) {
    std::string c = cmd;
    std::transform(c.begin(), c.end(), c.begin(),
                   [](unsigned char ch) { return (char)std::tolower(ch); });
    auto has = [&](const char* s) { return c.find(s) != std::string::npos; };

    if (has("git push") && (has("--force") || has(" -f ") || c.rfind(" -f") == c.size() - 3))
        return "git.push.force";
    if (has("git push"))                       return "git.push";
    if (has("git reset") && has("--hard"))     return "git.reset.hard";
    if (has("git clean"))                      return "git.clean";
    if (has("git checkout") || has("git switch")) return "git.checkout";
    if (has("rm -rf") || has("rm -fr") || has("rmdir /s") || has("rd /s"))
        return "fs.rm.recursive";
    if (has("drop table") || has("drop database")) return "db.drop";
    if (has("truncate table"))                 return "db.truncate";
    if (has("repo delete"))                    return "infra.repo.delete";
    if (has("release delete"))                 return "infra.release.delete";
    return "";
}

inline bool isDestructiveAction(const std::string& action) {
    return !action.empty();
}

} // namespace icmg::cli

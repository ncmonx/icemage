#pragma once
// Command densifier: rewrite a noisy command into a denser equivalent BEFORE it
// runs, so it emits less output for Tkil to filter. Two-stage token win:
// densify pre-exec (this) -> filter post-exec (Tkil).
//
//   git status   -> git status --porcelain=v2 --branch
//   git log      -> git log --oneline
//   pytest       -> pytest -q --tb=line
//   tsc          -> tsc --pretty false
//   pip list     -> pip list --format=freeze
//   npm list     -> npm list --depth=0
//
// Pure + idempotent: returns the ORIGINAL string when no rule matches, a guard
// trips (shell composition / expansion), or the user already gave a conflicting
// flag. Technique from token-savior; data here is plain CLI facts (no code/text
// reused) -- keeps icmg's license clean.
#include <string>
#include <vector>
#include <sstream>
#include <initializer_list>

namespace icmg::core {

// Shell composition / expansion we must never rewrite across (would change or
// corrupt semantics). If any is present, bail and return the command untouched.
inline bool hasShellComposition(const std::string& c) {
    static const char* bad[] = {"&&", "||", "|", ";", "$(", "`", "<<", "\n"};
    for (auto b : bad) if (c.find(b) != std::string::npos) return true;
    return false;
}

inline std::vector<std::string> densifyWords(const std::string& c) {
    std::vector<std::string> w; std::istringstream is(c); std::string t;
    while (is >> t) w.push_back(t);
    return w;
}

// True if any token equals a flag or begins "<flag>=" (so --tb matches --tb=line).
inline bool densifyHasFlag(const std::vector<std::string>& w,
                           std::initializer_list<const char*> flags) {
    for (const auto& tok : w)
        for (auto f : flags) {
            std::string s(f);
            if (tok == s || tok.rfind(s + "=", 0) == 0) return true;
        }
    return false;
}

inline std::string densifyCommand(const std::string& cmd) {
    if (hasShellComposition(cmd)) return cmd;
    auto w = densifyWords(cmd);
    if (w.empty()) return cmd;

    auto starts = [&](std::initializer_list<const char*> pre) {
        if (w.size() < pre.size()) return false;
        size_t i = 0;
        for (auto p : pre) { if (w[i] != p) return false; ++i; }
        return true;
    };
    auto append = [&](const std::string& flags) { return cmd + " " + flags; };

    if (starts({"git", "status"}) &&
        !densifyHasFlag(w, {"--porcelain", "-s", "--short"}))
        return append("--porcelain=v2 --branch");

    if (starts({"git", "log"}) &&
        !densifyHasFlag(w, {"--oneline", "--format", "--pretty", "--graph", "-p", "--stat"}))
        return append("--oneline");

    if (starts({"pip", "list"}) && !densifyHasFlag(w, {"--format"}))
        return append("--format=freeze");

    if ((starts({"npm", "list"}) || starts({"npm", "ls"})) && !densifyHasFlag(w, {"--depth"}))
        return append("--depth=0");

    bool is_pytest = (w[0] == "pytest") ||
                     (w.size() >= 3 && (w[0] == "python" || w[0] == "python3") &&
                      w[1] == "-m" && w[2] == "pytest");
    if (is_pytest && !densifyHasFlag(w, {"-v", "--verbose", "-q", "--quiet", "--tb"}))
        return append("-q --tb=line");

    if (w[0] == "tsc" && !densifyHasFlag(w, {"--pretty"}))
        return append("--pretty false");

    return cmd;
}

}  // namespace icmg::core

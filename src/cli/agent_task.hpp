#pragma once
// Pure, header-only task-string assembly for `icmg agent`.
//
// Bug (pre-fix): run() joined every arg whose first char != '-'. But the
// value-taking flags --timeout and --command carry a VALUE arg that does
// NOT start with '-' (e.g. "300", "claude --print"), so that value leaked
// into the task string. assembleTask() skips flags AND the single value
// arg immediately following --timeout / --command.

#include <string>
#include <vector>

namespace icmg::cli {

inline std::string assembleTask(const std::vector<std::string>& args) {
    std::string task;
    bool skipNext = false;  // value arg of a value-taking flag
    for (auto& a : args) {
        if (skipNext) { skipNext = false; continue; }
        if (a.empty() || a[0] == '-') {
            if (a == "--timeout" || a == "--command") skipNext = true;
            continue;
        }
        if (!task.empty()) task += " ";
        task += a;
    }
    return task;
}

}  // namespace icmg::cli

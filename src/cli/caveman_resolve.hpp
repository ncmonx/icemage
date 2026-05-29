// v1.66: per-project caveman resolution. Pure + header-only for unit tests.
//
// Precedence (most specific wins):
//   1. project OFF marker  (.icmg/caveman.off)   -> OFF, overrides global
//   2. project ON flag     (.icmg/caveman.flag)  -> ON
//   3. global ON flag      (~/.icmg/caveman.flag)-> ON
//   4. none                                      -> OFF (default)
//
// Lets each project be independent: a new project defaults OFF even if the
// global flag is on, by dropping a project OFF marker; or opts ON locally
// without touching the global state.

#pragma once
#include <string>

namespace icmg::cli {

struct CavemanState {
    bool        on = false;
    std::string level = "ultra";   // only meaningful when on
    std::string source;            // "project-off" | "project" | "global" | "none"
};

// Inputs: existence flags + the level string read from whichever ON flag
// applies (project flag preferred, else global). Pure — no IO.
inline CavemanState resolveCaveman(bool project_off_exists,
                                   bool project_on_exists,
                                   bool global_on_exists,
                                   const std::string& project_level,
                                   const std::string& global_level) {
    CavemanState s;
    if (project_off_exists) { s.on = false; s.source = "project-off"; return s; }
    if (project_on_exists)  { s.on = true;  s.level = project_level.empty() ? "ultra" : project_level; s.source = "project"; return s; }
    if (global_on_exists)   { s.on = true;  s.level = global_level.empty()  ? "ultra" : global_level;  s.source = "global";  return s; }
    s.on = false; s.source = "none";
    return s;
}

}  // namespace icmg::cli

// v1.78.1: per-project sayless resolution (renamed from caveman v1.66).
// Pure + header-only for unit tests.
//
// Precedence (most specific wins):
//   1. project OFF marker  (.icmg/sayless.off)   -> OFF, overrides global
//   2. project ON flag     (.icmg/sayless.flag)  -> ON
//   3. global ON flag      (~/.icmg/sayless.flag)-> ON
//   4. none                                      -> OFF (default)
//
// Lets each project be independent: a new project defaults OFF even if the
// global flag is on, by dropping a project OFF marker; or opts ON locally
// without touching the global state.
//
// Levels: lite | full | ultra (default) | hyper | wenyan-lite | wenyan-full | wenyan-ultra

#pragma once
#include <string>

namespace icmg::cli {

struct SaylessState {
    bool        on = false;
    std::string level = "ultra";   // only meaningful when on
    std::string source;            // "project-off" | "project" | "global" | "none"
};

// Inputs: existence flags + the level string read from whichever ON flag
// applies (project flag preferred, else global). Pure — no IO.
inline SaylessState resolveSayless(bool project_off_exists,
                                   bool project_on_exists,
                                   bool global_on_exists,
                                   const std::string& project_level,
                                   const std::string& global_level) {
    SaylessState s;
    if (project_off_exists) { s.on = false; s.source = "project-off"; return s; }
    if (project_on_exists)  { s.on = true;  s.level = project_level.empty() ? "ultra" : project_level; s.source = "project"; return s; }
    if (global_on_exists)   { s.on = true;  s.level = global_level.empty()  ? "ultra" : global_level;  s.source = "global";  return s; }
    s.on = false; s.source = "none";
    return s;
}

}  // namespace icmg::cli

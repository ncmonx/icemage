#pragma once
// Smart-install version-caching decision (pure, testable).
// Repeated `icmg install --system` should be a cheap no-op when the system
// binary is already at the running version. shouldReinstall() encodes that
// policy so install_cmd stays a thin caller; the version sentinel I/O lives in
// the command. Mirrors the header-only pure-helper pattern used across cli/.

#include <string>

namespace icmg::cli {

struct InstallDecision {
    bool        reinstall = true;
    std::string reason;
};

// running   = version of the binary being run (about to be installed)
// installed = version recorded for the current system install ("" if none)
// force     = user passed --force (always reinstall)
inline InstallDecision shouldReinstall(const std::string& running,
                                       const std::string& installed,
                                       bool force) {
    InstallDecision d;
    if (force) {
        d.reinstall = true;
        d.reason = "force requested -> reinstall";
        return d;
    }
    if (installed.empty()) {
        d.reinstall = true;
        d.reason = "no install recorded -> installing " + running;
        return d;
    }
    if (installed == running) {
        d.reinstall = false;
        d.reason = "system install up to date (" + running + ") -> skip";
        return d;
    }
    d.reinstall = true;
    d.reason = "version change " + installed + " -> " + running + " -> reinstall";
    return d;
}

} // namespace icmg::cli

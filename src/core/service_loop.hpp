#pragma once
// v1.1.0 Task 6.5: resident daemon ticker.
//
// Single long-running process at user logon → in-process schedule of
// backup / maintain / mirror / sentinel / shadow-upgrade. Replaces 5
// per-project schtasks → eliminates per-cycle subprocess fork → kills
// the remaining cmd/powershell popup flash at the root.
//
// State persisted to `<global>/service-state.json` so restart catches up
// on overdue ticks without losing track.

#include <atomic>
#include <string>

namespace icmg::core {

class ServiceLoop {
public:
    // Block on a 30-second ticker until shouldStop() flips true or the
    // process receives SIGTERM/Ctrl+C. Each tick checks per-task overdue
    // status; runs any that are overdue via Registry<BaseCommand>::create
    // (zero subprocess fork).
    int run();

    // Set by signal handler / `icmg service stop`.
    static void requestStop();
    static bool shouldStop();

private:
    void tickOnce();
};

// Returns absolute path to service-state.json under the icmg global dir.
std::string serviceStatePath();

// Returns absolute path to service.pid under the icmg global dir.
std::string servicePidPath();

} // namespace icmg::core

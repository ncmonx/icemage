#pragma once
// Singleton-guard naming for `icmg service`.
//
// Bug (zombie bloat): the guard used a `Global\` named mutex. Creating an
// object in the Global\ namespace requires SeCreateGlobalPrivilege, which a
// normal (non-elevated) interactive session lacks -> CreateMutexA fails with
// ERROR_ACCESS_DENIED (5) -> the guard fell OPEN -> every `service run` spawned
// by the per-prompt autostart hook survived. With N sessions this reached 20+
// live services, each 30s-ticking (DB-lock contention -> graph-update hang;
// path probes -> B:/ popup storm; duplicate pipe create -> ACCESS_DENIED).
//
// Fix: use the `Local\` namespace -- per-logon-session, but creatable WITHOUT
// elevation, so the guard actually holds. Sessions in one logon share it
// (correct dedup); distinct logon sessions get one service each (acceptable,
// far better than unbounded bloat).
#include <string>

namespace icmg::core {

inline std::string serviceMutexName(const std::string& user) {
    std::string u = user.empty() ? "default" : user;
    return "Local\\icmg-service-" + u;
}

} // namespace icmg::core

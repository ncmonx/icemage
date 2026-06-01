// v1.57 S2: persona DB at icmg.exe directory — implementation.

#include "persona_db.hpp"
#include "db.hpp"
#include "global_db.hpp"
#include "path_utils.hpp"

#include <memory>
#include <mutex>

namespace icmg::core {

namespace {

std::once_flag g_once;
std::unique_ptr<Db> g_db;          // exe-dir persona DB (null if unavailable)
bool g_available = false;

void initOnce() {
    std::call_once(g_once, [] {
        std::string path = personaDbPath();
        if (path.empty()) return;            // selfExePath failed
        try {
            auto db = std::make_unique<Db>(path);
            db->run(
                "CREATE TABLE IF NOT EXISTS user_personas("
                "  user_id TEXT PRIMARY KEY,"
                "  persona TEXT NOT NULL,"
                "  traits  TEXT,"
                "  updated_at INTEGER)");
            db->run("CREATE INDEX IF NOT EXISTS ix_user_personas_updated "
                    "ON user_personas(updated_at DESC)");
            g_db = std::move(db);
            g_available = true;
            // Make readable by all Win users / SYSTEM service.
            (void)relaxAclEveryone(path);
        } catch (...) {
            g_available = false;             // exe-dir not writable
        }
    });
}

}  // namespace

bool personaDbAvailable() {
    initOnce();
    return g_available;
}

Db& personaDb() {
    initOnce();
    if (!g_available || !g_db)
        throw std::runtime_error("persona DB unavailable at exe dir");
    return *g_db;
}

bool readPersona(const std::string& user_id,
                 std::string& persona_out,
                 std::string& traits_out) {
    // 1. Try exe-dir persona DB.
    if (personaDbAvailable()) {
        bool found = false;
        personaDb().query(
            "SELECT persona, traits FROM user_personas WHERE user_id=?",
            {user_id},
            [&](const Row& r) {
                if (r.size() >= 2) {
                    persona_out = r[0];
                    traits_out  = r[1];
                    found = true;
                }
            });
        if (found) return true;
    }
    // 2. Legacy fallback: global DB (pre-v1.57 personas).
    try {
        auto& gdb = GlobalDb::instance();
        gdb.init();
        bool found = false;
        gdb.db().query(
            "SELECT persona, traits FROM user_personas WHERE user_id=?",
            {user_id},
            [&](const Row& r) {
                if (r.size() >= 2) {
                    persona_out = r[0];
                    traits_out  = r[1];
                    found = true;
                }
            });
        return found;
    } catch (...) {
        return false;
    }
}

bool writePersona(const std::string& user_id,
                  const std::string& persona,
                  const std::string& traits) {
    const char* sql =
        "INSERT INTO user_personas(user_id,persona,traits,updated_at) "
        "VALUES(?,?,?,strftime('%s','now')) "
        "ON CONFLICT(user_id) DO UPDATE SET "
        "persona=excluded.persona, traits=excluded.traits, "
        "updated_at=excluded.updated_at";
    // Prefer exe-dir DB.
    if (personaDbAvailable()) {
        try {
            personaDb().run(sql, {user_id, persona, traits});
            return true;
        } catch (...) { /* fall through to global */ }
    }
    // Fallback: global DB.
    try {
        auto& gdb = GlobalDb::instance();
        gdb.init();
        gdb.db().run(sql, {user_id, persona, traits});
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace icmg::core

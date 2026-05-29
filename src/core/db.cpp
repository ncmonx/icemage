#include "db.hpp"
#include "migrator.hpp"
#include "embedded_migrations.hpp"
#include "path_utils.hpp"
#include "db_key.hpp"   // v1.76: encryption-at-rest key application
#include <sqlite3.h>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <sstream>
#include <filesystem>
#include <thread>
#include <chrono>

#ifndef _WIN32
#  include <sys/stat.h>
#endif

namespace fs = std::filesystem;
namespace icmg::core {

// ---- Db ----

Db::Db(const std::string& path) {
    int rc = sqlite3_open_v2(path.c_str(), &db_,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);
    checkRc(rc, "open " + path);

    // Restrict file permissions immediately after create
#ifndef _WIN32
    chmod(path.c_str(), 0600);
#endif

    // v1.76: encryption-at-rest. If enabled for this DB's scope, apply the
    // SQLCipher key BEFORE any other pragma/query. SQLCipher with no key behaves
    // exactly like plain SQLite, so when encryption is OFF this block is skipped
    // and existing plaintext DBs are untouched (back-compat).
    {
        auto enc = parseEncryptionConfig(readEncryptConfigText());
        bool global_db = path.find("global.db") != std::string::npos;
        bool in_scope = enc.enabled &&
            (enc.scope == "both" || (enc.scope == "project" && !global_db));
        if (in_scope) {
            std::string key = resolveDbKey(enc);
            if (!key.empty()) {
                std::string pragma = "PRAGMA key=\"x'" + key + "'\";";
                sqlite3_exec(db_, pragma.c_str(), nullptr, nullptr, nullptr);
                // Probe: a wrong key makes the first read of sqlite_master fail
                // (SQLCipher HMAC). Map that to a clean, actionable error instead
                // of a downstream crash. A fresh/correctly-keyed DB passes.
                if (sqlite3_exec(db_, "SELECT count(*) FROM sqlite_master;",
                                 nullptr, nullptr, nullptr) != SQLITE_OK) {
                    throw DbError("DB is encrypted and the key doesn't match or "
                                  "is missing. Run 'icmg encrypt status'.");
                }
            }
        }
    }

    applyPragmas();

    // Phase 59 T2: defensive schema repair — auto-run pending embedded migrations
    // if user_version lags behind embedded latest. Closes "row_version missing"
    // bug-class when DB opened via path that bypasses ensureProjectDb.
    // No-op when up-to-date (one PRAGMA user_version read).
    //
    // Skip when user_version == 0: fresh DB OR test fixture that creates raw
    // schema without versioning. ensureProjectDb runs full Migrator on real
    // brand-new project DBs; this defensive path only catches partially-
    // migrated DBs (cur > 0 < latest).
    try {
        int cur = userVersion();
        if (cur == 0) throw 0;  // skip — fresh / unversioned DB
        int latest = 0;
        for (auto& [ver, _sql] : embeddedMigrations()) latest = std::max(latest, ver);
        if (cur < latest) {
            for (auto& [ver, sql] : embeddedMigrations()) {
                if (ver <= cur) continue;
                run("BEGIN TRANSACTION");
                try {
                    // Inline strip of BEGIN/COMMIT lines that some migrations include.
                    std::istringstream in(sql);
                    std::ostringstream out;
                    std::string line;
                    while (std::getline(in, line)) {
                        std::string t = line;
                        auto p = t.find_first_not_of(" \t\r");
                        if (p != std::string::npos) t = t.substr(p);
                        std::string u = t;
                        for (auto& c : u) c = (char)std::toupper((unsigned char)c);
                        if (u == "BEGIN" || u == "BEGIN;" ||
                            u == "BEGIN TRANSACTION" || u == "BEGIN TRANSACTION;" ||
                            u == "COMMIT" || u == "COMMIT;" ||
                            u == "ROLLBACK" || u == "ROLLBACK;") continue;
                        out << line << '\n';
                    }
                    run(out.str());
                    run("COMMIT");
                    setUserVersion(ver);
                } catch (...) {
                    try { run("ROLLBACK"); } catch (...) {}
                    // Schema-repair failure should not block Db open; user
                    // can run `icmg doctor` for diagnosis.
                    break;
                }
            }
        }
    } catch (...) {
        // Defensive only; never block Db open on schema-repair failure.
    }
}

Db::~Db() {
    clearStmtCache();
    if (db_) sqlite3_close(db_);
}

Db::Db(Db&& o) noexcept
    : db_(o.db_)
    , stmt_list_(std::move(o.stmt_list_))
    , stmt_map_(std::move(o.stmt_map_))
{
    o.db_ = nullptr;
}
Db& Db::operator=(Db&& o) noexcept {
    if (this != &o) {
        clearStmtCache();
        if (db_) sqlite3_close(db_);
        db_ = o.db_;
        stmt_list_ = std::move(o.stmt_list_);
        stmt_map_  = std::move(o.stmt_map_);
        o.db_ = nullptr;
    }
    return *this;
}

// ---- Phase A4 (v0.53.2): prepared-statement LRU cache ----

void Db::clearStmtCache() {
    for (auto& e : stmt_list_) {
        if (e.stmt) sqlite3_finalize(e.stmt);
    }
    stmt_list_.clear();
    stmt_map_.clear();
}

sqlite3_stmt* Db::getCachedStmt(const std::string& sql) const {
    auto it = stmt_map_.find(sql);
    if (it != stmt_map_.end()) {
        // Cache hit — move to front (MRU) + reset.
        stmt_list_.splice(stmt_list_.begin(), stmt_list_, it->second);
        sqlite3_stmt* stmt = it->second->stmt;
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
        return stmt;
    }
    // Cache miss — prepare + insert at front.
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        if (stmt) sqlite3_finalize(stmt);
        return nullptr;
    }
    stmt_list_.push_front({sql, stmt});
    stmt_map_[sql] = stmt_list_.begin();

    // Evict LRU tail if over cap.
    while (stmt_list_.size() > kPreparedCap) {
        auto& back = stmt_list_.back();
        if (back.stmt) sqlite3_finalize(back.stmt);
        stmt_map_.erase(back.sql);
        stmt_list_.pop_back();
    }
    return stmt;
}

void Db::releaseCachedStmt(sqlite3_stmt* stmt) const {
    if (stmt) {
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
}

void Db::applyPragmas() {
    // WAL mode for concurrent read + single writer
    run("PRAGMA journal_mode=WAL");
    run("PRAGMA synchronous=NORMAL");
    run("PRAGMA foreign_keys=ON");
    // Phase 28+ — `graph update --parallel` spawns N subprocess writers
    // contending for single-writer lock. 30s gives time to drain queue.
    run("PRAGMA busy_timeout=30000");
    run("PRAGMA cache_size=-8000"); // 8 MB
    run("PRAGMA page_size=4096");       // effective only on new DBs; no-op on existing
    run("PRAGMA mmap_size=268435456");  // 256 MB mmap — read pages skip syscall
    // 100 pages (400KB) — checkpoint frequently so WAL cannot bloat to GBs
    // if concurrent hook processes hold write locks. Old default (1000) allowed
    // unbounded growth when double-backgrounded hooks spawned hundreds of writers.
    run("PRAGMA wal_autocheckpoint=100");
}

void Db::checkRc(int rc, const std::string& ctx) const {
    if (rc != SQLITE_OK) {
        std::string msg = ctx + ": " + sqlite3_errmsg(db_);
        throw DbError(msg);
    }
}

// SQLite's busy_timeout already retries internally, but on POSIX it can return
// SQLITE_BUSY before timeout when WAL writer lock is held by another connection
// in a transaction. Wrap step() with explicit backoff to absorb those bursts.
static int stepWithRetry(sqlite3_stmt* stmt) {
    int rc;
    int backoff_ms = 5;
    for (int attempt = 0; attempt < 60; ++attempt) {   // ~6s total
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED) return rc;
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        if (backoff_ms < 200) backoff_ms = (int)(backoff_ms * 1.6);
        sqlite3_reset(stmt);
    }
    return rc;
}

void Db::run(const std::string& sql) {
    char* errmsg = nullptr;
    int rc;
    int backoff_ms = 5;
    for (int attempt = 0; attempt < 60; ++attempt) {
        rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
        if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED) break;
        if (errmsg) { sqlite3_free(errmsg); errmsg = nullptr; }
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        if (backoff_ms < 200) backoff_ms = (int)(backoff_ms * 1.6);
    }
    if (rc != SQLITE_OK) {
        std::string msg = sql.substr(0, 60) + ": " + (errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        throw DbError(msg);
    }
}

void Db::run(const std::string& sql, const std::vector<std::string>& params) {
    // Phase A4: use cached prepared stmt — avoids re-prepare on hot path.
    sqlite3_stmt* stmt = getCachedStmt(sql);
    if (!stmt) throw DbError("prepare: " + sql.substr(0, 60) + ": " + sqlite3_errmsg(db_));

    for (int i = 0; i < (int)params.size(); ++i) {
        sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
    }

    int rc = stepWithRetry(stmt);
    releaseCachedStmt(stmt);  // reset, do NOT finalize
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw DbError("exec: " + sql.substr(0, 60) + ": " + sqlite3_errmsg(db_));
    }
}

void Db::query(const std::string& sql,
               const std::vector<std::string>& params,
               RowCallback cb) {
    // Phase A4: cached prepared stmt.
    sqlite3_stmt* stmt = getCachedStmt(sql);
    if (!stmt) throw DbError("prepare query: " + sql.substr(0, 60) + ": " + sqlite3_errmsg(db_));

    for (int i = 0; i < (int)params.size(); ++i) {
        sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
    }

    int rc = 0;

    // Retry initial step on BUSY/LOCKED. Once we have first row, subsequent
    // steps within same statement won't see contention.
    int backoff_ms = 5;
    for (int attempt = 0; attempt < 60; ++attempt) {
        rc = sqlite3_step(stmt);
        if (rc != SQLITE_BUSY && rc != SQLITE_LOCKED) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        if (backoff_ms < 200) backoff_ms = (int)(backoff_ms * 1.6);
        sqlite3_reset(stmt);
        for (int i = 0; i < (int)params.size(); ++i)
            sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
    }
    while (rc == SQLITE_ROW) {
        int cols = sqlite3_column_count(stmt);
        Row row;
        row.reserve(cols);
        for (int c = 0; c < cols; ++c) {
            const unsigned char* val = sqlite3_column_text(stmt, c);
            row.push_back(val ? reinterpret_cast<const char*>(val) : "");
        }
        cb(row);
        rc = sqlite3_step(stmt);
    }

    releaseCachedStmt(stmt);  // Phase A4: reset, do NOT finalize
    if (rc != SQLITE_DONE) {
        throw DbError("query step: " + sql.substr(0, 60) + ": " + sqlite3_errmsg(db_));
    }
}

int64_t Db::lastInsertId() const {
    return static_cast<int64_t>(sqlite3_last_insert_rowid(db_));
}

int Db::userVersion() const {
    int ver = 0;
    sqlite3_stmt* stmt = nullptr;
    sqlite3_prepare_v2(db_, "PRAGMA user_version", -1, &stmt, nullptr);
    if (sqlite3_step(stmt) == SQLITE_ROW) ver = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return ver;
}

void Db::setUserVersion(int v) {
    std::string sql = "PRAGMA user_version=" + std::to_string(v);
    run(sql);
}

// ---- ensureProjectDb ----

void ensureProjectDb(const std::string& db_path) {
    fs::path p(db_path);
    fs::path dir = p.parent_path();

    bool dir_created = false;
    if (!fs::exists(dir)) {
        fs::create_directories(dir);
        dir_created = true;
#ifndef _WIN32
        chmod(dir.string().c_str(), 0700);
#endif
    }

    Db db(db_path);
    Migrator migrator;
    migrator.runAll(db);

    // v1.56 hotfix: relax Win ACL on the .icmg directory AND the DB file so
    // BUILTIN\Users + SYSTEM gain full control. Without this, a DB created
    // by Win user A is invisible to user B (or to a SYSTEM service spawned
    // by a scheduled task), and mirror-a/mirror-b backups inherit the same
    // tight DACL — causing "must have Read permissions" failures across
    // multi-user / service contexts. No-op on Linux/macOS.
    if (dir_created) (void)relaxAclEveryone(dir.string());
    (void)relaxAclEveryone(db_path);
}

} // namespace icmg::core

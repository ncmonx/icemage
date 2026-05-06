#include "db.hpp"
#include "migrator.hpp"
#include "path_utils.hpp"
#include <sqlite3.h>
#include <stdexcept>
#include <sstream>
#include <filesystem>

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

    applyPragmas();
}

Db::~Db() {
    if (db_) sqlite3_close(db_);
}

Db::Db(Db&& o) noexcept : db_(o.db_) { o.db_ = nullptr; }
Db& Db::operator=(Db&& o) noexcept {
    if (this != &o) { if (db_) sqlite3_close(db_); db_ = o.db_; o.db_ = nullptr; }
    return *this;
}

void Db::applyPragmas() {
    // WAL mode for concurrent read + single writer
    run("PRAGMA journal_mode=WAL");
    run("PRAGMA synchronous=NORMAL");
    run("PRAGMA foreign_keys=ON");
    run("PRAGMA busy_timeout=5000");
    run("PRAGMA cache_size=-8000"); // 8 MB
}

void Db::checkRc(int rc, const std::string& ctx) const {
    if (rc != SQLITE_OK) {
        std::string msg = ctx + ": " + sqlite3_errmsg(db_);
        throw DbError(msg);
    }
}

void Db::run(const std::string& sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string msg = sql.substr(0, 60) + ": " + (errmsg ? errmsg : "?");
        sqlite3_free(errmsg);
        throw DbError(msg);
    }
}

void Db::run(const std::string& sql, const std::vector<std::string>& params) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) throw DbError("prepare: " + sql.substr(0, 60) + ": " + sqlite3_errmsg(db_));

    for (int i = 0; i < (int)params.size(); ++i) {
        sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw DbError("exec: " + sql.substr(0, 60) + ": " + sqlite3_errmsg(db_));
    }
}

void Db::query(const std::string& sql,
               const std::vector<std::string>& params,
               RowCallback cb) {
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) throw DbError("prepare query: " + sql.substr(0, 60) + ": " + sqlite3_errmsg(db_));

    for (int i = 0; i < (int)params.size(); ++i) {
        sqlite3_bind_text(stmt, i + 1, params[i].c_str(), -1, SQLITE_TRANSIENT);
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        int cols = sqlite3_column_count(stmt);
        Row row;
        row.reserve(cols);
        for (int c = 0; c < cols; ++c) {
            const unsigned char* val = sqlite3_column_text(stmt, c);
            row.push_back(val ? reinterpret_cast<const char*>(val) : "");
        }
        cb(row);
    }

    sqlite3_finalize(stmt);
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

    if (!fs::exists(dir)) {
        fs::create_directories(dir);
#ifndef _WIN32
        chmod(dir.string().c_str(), 0700);
#endif
    }

    Db db(db_path);
    Migrator migrator;
    migrator.runAll(db);
}

} // namespace icmg::core

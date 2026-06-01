#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>
#include <cstdint>
#include <list>
#include <unordered_map>

namespace icmg::core {

class DbError : public std::runtime_error {
public:
    explicit DbError(const std::string& msg) : std::runtime_error(msg) {}
};

using Row = std::vector<std::string>;
using RowCallback = std::function<void(const Row&)>;

class Db {
public:
    explicit Db(const std::string& path);
    ~Db();
    Db(const Db&) = delete;
    Db& operator=(const Db&) = delete;
    Db(Db&&) noexcept;
    Db& operator=(Db&&) noexcept;

    void run(const std::string& sql);
    void run(const std::string& sql, const std::vector<std::string>& params);
    void query(const std::string& sql,
               const std::vector<std::string>& params,
               RowCallback cb);

    int64_t lastInsertId() const;
    int     userVersion() const;
    void    setUserVersion(int v);

    bool isOpen() const { return db_ != nullptr; }

    // Raw handle access — for binary BLOB binding (embeddings, etc.).
    // Use sparingly; prefer run/query for normal text params.
    sqlite3* handle() { return db_; }

private:
    sqlite3* db_ = nullptr;

    // Phase A4 (v0.53.2): prepared-statement LRU cache.
    // Avoids re-prepare cost on hot-path queries (recall, drift, path-context).
    // Cap = 50. Destructor finalizes all cached stmts.
    struct LruEntry {
        std::string sql;
        sqlite3_stmt* stmt;
    };
    mutable std::list<LruEntry> stmt_list_;
    mutable std::unordered_map<std::string,
                               std::list<LruEntry>::iterator> stmt_map_;
    static constexpr size_t kPreparedCap = 50;

    // Get cached prepared stmt or prepare + insert. Resets stmt + clears
    // bindings before return. Returns nullptr on prepare failure.
    sqlite3_stmt* getCachedStmt(const std::string& sql) const;
    // Release stmt back to cache: reset state (does NOT finalize).
    void releaseCachedStmt(sqlite3_stmt* stmt) const;
    // Destructor helper: finalize all cached stmts.
    void clearStmtCache();

    void checkRc(int rc, const std::string& ctx) const;
    void applyPragmas();
};

// Helper: auto-create .icmg/ dir + run migrations
void ensureProjectDb(const std::string& db_path);

} // namespace icmg::core

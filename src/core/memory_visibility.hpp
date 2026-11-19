// #288: per-author memory visibility.
//
// Two OS accounts that open the same project folder share one brain
// (.icmg/data.db). Every memory row now carries its author (the OS account)
// and a visibility: `private` rows are seen only by their author, `team` rows
// by everyone. Rows with no author (written before #288, synced or imported)
// count as `team`, which is the old behaviour.
//
// Every read path that surfaces memory content filters with kVisibleSql, an
// SQL function registered on each Db connection, so the rule lives here once.
#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

struct sqlite3;

namespace icmg::core {

class Db;

inline constexpr const char* kVisibilityTeam    = "team";
inline constexpr const char* kVisibilityPrivate = "private";

// WHERE-clause predicate over memory_nodes (unaliased columns).
inline constexpr const char* kVisibleSql = "icmg_can_see(visibility, author)";

// Drop-in FROM target for raw SQL reads: `"SELECT ... FROM " ICMG_VISIBLE_MEMORY
// " WHERE ..."`. The subquery is aliased memory_nodes, so existing column names
// and WHERE clauses (including unparenthesised ORs) keep working unchanged.
#define ICMG_VISIBLE_MEMORY \
    "(SELECT * FROM memory_nodes WHERE icmg_can_see(visibility, author)) AS memory_nodes"

// Same predicate for an aliased table, e.g. visibleSqlFor("m").
std::string visibleSqlFor(const std::string& alias);

// The OS account running icmg: Windows user SID ("S-1-5-21-..."), POSIX
// "uid:<n>". ICMG_USER overrides it. Resolved once per process.
const std::string& currentAuthor();

// Test hook: replace the cached author ("" restores the real one).
void setCurrentAuthorForTest(const std::string& author);

// `recall --all-authors`: lift the filter for this process (explicit opt-in).
void setSeeAllAuthors(bool on);
bool seeAllAuthors();

// Scoped opt-in: commands can run inside a long-lived daemon process, so the
// flag must never outlive the command that asked for it.
class SeeAllAuthorsScope {
public:
    explicit SeeAllAuthorsScope(bool on) : prev_(seeAllAuthors()) { if (on) setSeeAllAuthors(true); }
    ~SeeAllAuthorsScope() { setSeeAllAuthors(prev_); }
    SeeAllAuthorsScope(const SeeAllAuthorsScope&) = delete;
    SeeAllAuthorsScope& operator=(const SeeAllAuthorsScope&) = delete;
private:
    bool prev_;
};

// The rule itself: team, author-less and own rows are visible.
bool canSee(std::string_view visibility, std::string_view author);

// Default visibility for a new memory, picked from its topic: session /
// persona / feeling style notes are private, everything else (decisions,
// known issues, corrections, architecture, graph) is team.
const char* defaultVisibilityForTopic(std::string_view topic);

// Normalises user input ("private" / "team"); returns nullptr when invalid.
const char* parseVisibility(std::string_view v);

// Adds author/visibility to an existing memory_nodes that lacks them
// (hand-made fixtures, DBs that skipped migration 0047). No-op otherwise.
void ensureVisibilityColumns(Db& db);

// `icmg memory retag`: set the visibility of the given ids, or of every row
// whose topic starts with topicPrefix (ids empty). Only rows the caller owns,
// or author-less legacy rows (which the caller then claims), are changed;
// another account's rows are never touched. Returns rows changed.
int retagMemories(Db& db, const std::vector<int64_t>& ids, const std::string& topicPrefix,
                  const char* visibility);

// Folder-safe name of the OS account (ICMG_USER, else the login name), used
// for per-user `.remember/<user>/`. Never empty.
std::string currentUserDirName();

// The `.remember` directory to read handoff notes from: `<root>/.remember/<user>`
// when it exists (shared project folder, #288), else the legacy shared
// `<root>/.remember`.
std::filesystem::path rememberDir(const std::filesystem::path& root = ".");

// Registers icmg_can_see() on a connection. Called by Db's constructor.
void registerVisibilitySql(sqlite3* db);

}  // namespace icmg::core

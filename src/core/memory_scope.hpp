#pragma once
// #292: memory namespaces (tenant scope) + trust level (provenance).
//
// One service process can serve many callers; each call passes the namespaces
// it may touch (`--ns user:12,team:finance`). Recall then sees ONLY those
// namespaces; the token "shared" (kSharedNamespace) names the default ''
// namespace every pre-#292 memory lives in. No --ns = no namespace filter
// (the old behaviour). The SQL side is `icmg_in_ns(namespace)`, folded into
// the shared visibility predicate (memory_visibility.hpp), so every raw read
// that already honours #288 honours namespaces too.
//
// Trust: memories whose source is web/file/fetch/ingest/import default to
// 'untrusted'; recall marks them (kUntrustedMarker) so a later run cannot be
// prompt-injected through its own memory.
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

struct sqlite3;

namespace icmg::core {

class Db;

inline constexpr const char* kSharedNamespace  = "shared";
inline constexpr const char* kNamespaceEnv     = "ICMG_NS";
inline constexpr const char* kTrustTrusted     = "trusted";
inline constexpr const char* kTrustUntrusted   = "untrusted";
inline constexpr const char* kUntrustedMarker  = "[UNTRUSTED]";
inline constexpr size_t      kMaxNamespaceLen  = 128;

// Parses "user:12, team:finance,shared" -> {"user:12","team:finance",""}.
// Empty / whitespace entries are dropped, duplicates removed. Returns false
// (and fills *err) for a name that is too long or holds control characters.
bool parseNamespaces(std::string_view csv, std::vector<std::string>* out,
                     std::string* err = nullptr);

// Active recall/store scope of this process. Empty list = no filter.
void setActiveNamespaces(const std::vector<std::string>& ns);
std::vector<std::string> activeNamespaces();
bool namespaceFilterOn();

// Namespace a new memory is written into: the single active namespace, or ''
// when none is active. Callers reject --ns with several names on store.
std::string storeNamespace();

// Scoped set (commands may run inside a long-lived process): restores the
// previous scope on exit. An empty list leaves the current scope alone.
class NamespaceScope {
public:
    explicit NamespaceScope(const std::vector<std::string>& ns);
    ~NamespaceScope();
    NamespaceScope(const NamespaceScope&) = delete;
    NamespaceScope& operator=(const NamespaceScope&) = delete;
private:
    std::vector<std::string> prev_;
};

// Reads --ns from argv (or $ICMG_NS when absent). Returns false + *err on a
// bad name; *out left empty when neither is set.
bool namespacesFromArgs(const std::vector<std::string>& args,
                        std::vector<std::string>* out, std::string* err);

// The rule: no filter -> visible; else namespace must be in the active list.
bool inActiveNamespace(std::string_view ns);

// Display form of a stored namespace ('' -> "shared").
std::string namespaceLabel(std::string_view ns);

// Trust.
const char* defaultTrustForSource(std::string_view source);
const char* parseTrust(std::string_view t);   // nullptr when invalid
bool isUntrusted(std::string_view trust);

// "7d", "12h", "30m", "3600" (seconds), "2w" -> seconds; -1 when invalid.
int64_t parseTtlSeconds(std::string_view ttl);

// Adds namespace/trust to an existing memory_nodes that lacks them
// (fixtures, DBs that skipped migration 0048). No-op otherwise.
void ensureScopeColumns(Db& db);

// Registers icmg_in_ns(namespace) on a connection.
void registerScopeSql(sqlite3* db);

}  // namespace icmg::core

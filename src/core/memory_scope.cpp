// #292: memory namespaces + trust. See memory_scope.hpp.
#include "memory_scope.hpp"
#include "db.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>

namespace icmg::core {

namespace {

// Sources whose content came from outside the conversation: untrusted.
constexpr const char* kUntrustedSources[] = {
    "web", "file", "fetch", "ingest", "import", "url", "doc", "tool",
};

std::mutex g_ns_mu;
std::vector<std::string> g_active;  // guarded by g_ns_mu
bool g_filter_on = false;           // guarded by g_ns_mu

std::string trim(std::string_view s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view textArg(sqlite3_value* v) {
    const auto* p = sqlite3_value_text(v);
    if (!p) return {};
    return {reinterpret_cast<const char*>(p), static_cast<size_t>(sqlite3_value_bytes(v))};
}

void inNsSqlFn(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    if (argc != 1) { sqlite3_result_int(ctx, 1); return; }
    sqlite3_result_int(ctx, inActiveNamespace(textArg(argv[0])) ? 1 : 0);
}

}  // namespace

bool parseNamespaces(std::string_view csv, std::vector<std::string>* out, std::string* err) {
    out->clear();
    size_t start = 0;
    while (start <= csv.size()) {
        size_t comma = csv.find(',', start);
        if (comma == std::string_view::npos) comma = csv.size();
        std::string name = trim(csv.substr(start, comma - start));
        start = comma + 1;
        if (name.empty()) continue;
        if (name.size() > kMaxNamespaceLen) {
            if (err) *err = "namespace longer than " + std::to_string(kMaxNamespaceLen) + " chars";
            return false;
        }
        for (char c : name) {
            if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
                if (err) *err = "namespace holds a control character";
                return false;
            }
        }
        if (name == kSharedNamespace) name.clear();
        if (std::find(out->begin(), out->end(), name) == out->end()) out->push_back(name);
    }
    return true;
}

void setActiveNamespaces(const std::vector<std::string>& ns) {
    std::lock_guard<std::mutex> lk(g_ns_mu);
    g_active = ns;
    g_filter_on = !ns.empty();
}

std::vector<std::string> activeNamespaces() {
    std::lock_guard<std::mutex> lk(g_ns_mu);
    return g_active;
}

bool namespaceFilterOn() {
    std::lock_guard<std::mutex> lk(g_ns_mu);
    return g_filter_on;
}

std::string storeNamespace() {
    std::lock_guard<std::mutex> lk(g_ns_mu);
    return g_active.empty() ? std::string() : g_active.front();
}

NamespaceScope::NamespaceScope(const std::vector<std::string>& ns) : prev_(activeNamespaces()) {
    if (!ns.empty()) setActiveNamespaces(ns);
}

NamespaceScope::~NamespaceScope() { setActiveNamespaces(prev_); }

bool namespacesFromArgs(const std::vector<std::string>& args,
                        std::vector<std::string>* out, std::string* err) {
    out->clear();
    std::string csv;
    bool given = false;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--ns" && i + 1 < args.size()) { csv = args[++i]; given = true; }
        else if (a.rfind("--ns=", 0) == 0)      { csv = a.substr(5); given = true; }
    }
    if (!given) {
        const char* env = std::getenv(kNamespaceEnv);
        if (!env || !*env) return true;
        csv = env;
    }
    if (!parseNamespaces(csv, out, err)) return false;
    if (out->empty()) {
        if (err) *err = "--ns needs at least one namespace (use 'shared' for the default one)";
        return false;
    }
    return true;
}

bool inActiveNamespace(std::string_view ns) {
    std::lock_guard<std::mutex> lk(g_ns_mu);
    if (!g_filter_on) return true;
    for (const auto& a : g_active)
        if (a == ns) return true;
    return false;
}

std::string namespaceLabel(std::string_view ns) {
    return ns.empty() ? std::string(kSharedNamespace) : std::string(ns);
}

const char* defaultTrustForSource(std::string_view source) {
    const std::string s = lower(source);
    for (const char* u : kUntrustedSources) {
        const std::string_view p(u);
        if (s == p || s.rfind(std::string(p) + ":", 0) == 0) return kTrustUntrusted;
    }
    return kTrustTrusted;
}

const char* parseTrust(std::string_view t) {
    const std::string s = lower(trim(t));
    if (s == kTrustTrusted)   return kTrustTrusted;
    if (s == kTrustUntrusted) return kTrustUntrusted;
    return nullptr;
}

bool isUntrusted(std::string_view trust) { return lower(trust) == kTrustUntrusted; }

int64_t parseTtlSeconds(std::string_view ttl) {
    const std::string s = lower(trim(ttl));
    if (s.empty()) return -1;
    size_t digits = 0;
    while (digits < s.size() && std::isdigit(static_cast<unsigned char>(s[digits]))) ++digits;
    if (digits == 0 || digits > 9) return -1;
    const int64_t n = std::stoll(s.substr(0, digits));
    const std::string unit = s.substr(digits);
    int64_t mult = 0;
    if (unit.empty() || unit == "s") mult = 1;
    else if (unit == "m") mult = 60;
    else if (unit == "h") mult = 3600;
    else if (unit == "d") mult = 86400;
    else if (unit == "w") mult = 7 * 86400;
    else return -1;
    return n > 0 ? n * mult : -1;
}

void ensureScopeColumns(Db& db) {
    int cols = 0;
    bool hasNs = false, hasTrust = false;
    db.query("PRAGMA table_info(memory_nodes)", {}, [&](const Row& r) {
        ++cols;
        if (r.size() > 1 && r[1] == "namespace") hasNs = true;
        if (r.size() > 1 && r[1] == "trust") hasTrust = true;
    });
    if (cols == 0) return;
    if (!hasNs)    db.run("ALTER TABLE memory_nodes ADD COLUMN namespace TEXT NOT NULL DEFAULT ''");
    if (!hasTrust) db.run("ALTER TABLE memory_nodes ADD COLUMN trust TEXT NOT NULL DEFAULT ''");
}

void registerScopeSql(sqlite3* db) {
    if (!db) return;
    // Not deterministic: the answer depends on the process scope.
    sqlite3_create_function_v2(db, "icmg_in_ns", 1, SQLITE_UTF8, nullptr,
                               &inNsSqlFn, nullptr, nullptr, nullptr);
}

}  // namespace icmg::core

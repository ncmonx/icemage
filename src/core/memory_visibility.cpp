#include "memory_visibility.hpp"
#include "db.hpp"
#include "path_u8.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <mutex>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <sddl.h>
#  include "win_utf8.hpp"
#  ifdef _MSC_VER
#    pragma comment(lib, "advapi32.lib")
#  endif
#else
#  include <unistd.h>
#endif

namespace icmg::core {

namespace {

// Topic prefixes (case-insensitive) that default to private: per-person
// session, persona, feeling and telemetry notes. Everything else is team.
constexpr const char* kPrivateTopicPrefixes[] = {
    "session", "log-saved", "memoir", "moment", "persona", "chat", "reflection",
    "claudy", "feeling", "mood", "identity", "profile", "handoff", "journal",
    "diary", "perf-", "context-", "context:", "_",
};

constexpr const char* kAuthorEnv = "ICMG_USER";
constexpr const char* kRememberDir = ".remember";
constexpr const char* kUnsafeDirChars = "\\/:*?\"<>|";

std::mutex g_author_mu;
std::string g_author;            // guarded by g_author_mu
bool g_author_resolved = false;  // guarded by g_author_mu
std::atomic<bool> g_see_all{false};

std::string resolveOsAuthor() {
    if (const char* env = std::getenv(kAuthorEnv); env && *env) return env;
#ifdef _WIN32
    std::string out;
    HANDLE tok = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
        DWORD len = 0;
        GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
        if (len > 0) {
            std::string buf(len, '\0');
            if (GetTokenInformation(tok, TokenUser, buf.data(), len, &len)) {
                const auto* tu = reinterpret_cast<const TOKEN_USER*>(buf.data());
                LPWSTR sid = nullptr;
                if (ConvertSidToStringSidW(tu->User.Sid, &sid)) {
                    out = wideToUtf8(sid);
                    LocalFree(sid);
                }
            }
        }
        CloseHandle(tok);
    }
    return out;
#else
    return "uid:" + std::to_string(static_cast<unsigned long>(getuid()));
#endif
}

bool startsWithNoCase(std::string_view s, std::string_view prefix) {
    if (s.size() < prefix.size()) return false;
    for (size_t i = 0; i < prefix.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(s[i])) !=
            std::tolower(static_cast<unsigned char>(prefix[i])))
            return false;
    return true;
}

std::string_view textArg(sqlite3_value* v) {
    const auto* p = sqlite3_value_text(v);
    if (!p) return {};
    return {reinterpret_cast<const char*>(p), static_cast<size_t>(sqlite3_value_bytes(v))};
}

void canSeeSqlFn(sqlite3_context* ctx, int argc, sqlite3_value** argv) {
    if (argc != 2) { sqlite3_result_int(ctx, 1); return; }
    sqlite3_result_int(ctx, canSee(textArg(argv[0]), textArg(argv[1])) ? 1 : 0);
}

// LIKE pattern for "starts with prefix", escaping % _ and the escape char.
std::string likePrefix(const std::string& prefix) {
    std::string out;
    for (char c : prefix) {
        if (c == '%' || c == '_' || c == '\\') out += '\\';
        out += c;
    }
    return out + "%";
}

}  // namespace

std::string visibleSqlFor(const std::string& alias) {
    if (alias.empty()) return kVisibleSql;
    return "icmg_can_see(" + alias + ".visibility, " + alias + ".author)";
}

const std::string& currentAuthor() {
    std::lock_guard<std::mutex> lk(g_author_mu);
    if (!g_author_resolved) {
        g_author = resolveOsAuthor();
        g_author_resolved = true;
    }
    return g_author;
}

void setCurrentAuthorForTest(const std::string& author) {
    std::lock_guard<std::mutex> lk(g_author_mu);
    g_author = author.empty() ? resolveOsAuthor() : author;
    g_author_resolved = true;
}

void setSeeAllAuthors(bool on) { g_see_all.store(on); }
bool seeAllAuthors() { return g_see_all.load(); }

bool canSee(std::string_view visibility, std::string_view author) {
    if (g_see_all.load()) return true;
    if (visibility != kVisibilityPrivate) return true;  // team / legacy / NULL
    if (author.empty()) return true;                     // author-less row = team
    return author == currentAuthor();
}

const char* defaultVisibilityForTopic(std::string_view topic) {
    for (const char* p : kPrivateTopicPrefixes)
        if (startsWithNoCase(topic, p)) return kVisibilityPrivate;
    return kVisibilityTeam;
}

const char* parseVisibility(std::string_view v) {
    if (v == kVisibilityPrivate) return kVisibilityPrivate;
    if (v == kVisibilityTeam) return kVisibilityTeam;
    return nullptr;
}

void ensureVisibilityColumns(Db& db) {
    int cols = 0;
    bool hasAuthor = false, hasVis = false;
    db.query("PRAGMA table_info(memory_nodes)", {}, [&](const Row& r) {
        ++cols;
        if (r.size() > 1 && r[1] == "author") hasAuthor = true;
        if (r.size() > 1 && r[1] == "visibility") hasVis = true;
    });
    if (cols == 0) return;
    if (!hasAuthor) db.run("ALTER TABLE memory_nodes ADD COLUMN author TEXT NOT NULL DEFAULT ''");
    if (!hasVis) db.run(std::string("ALTER TABLE memory_nodes ADD COLUMN visibility TEXT NOT NULL DEFAULT '") +
                        kVisibilityTeam + "'");
}

int retagMemories(Db& db, const std::vector<int64_t>& ids, const std::string& topicPrefix,
                  const char* visibility) {
    if (!visibility || (ids.empty() && topicPrefix.empty())) return 0;
    const std::string& me = currentAuthor();
    // author='' -> claim it; author=me -> mine; anything else is left alone.
    const std::string set = "UPDATE memory_nodes SET visibility = ?, "
                            "author = CASE WHEN author = '' THEN ? ELSE author END "
                            "WHERE deleted_at IS NULL AND (author = '' OR author = ?) AND ";
    int changed = 0;
    auto countChanges = [&] {
        db.query("SELECT changes()", {}, [&](const Row& r) {
            if (!r.empty()) changed += std::stoi(r[0]);
        });
    };
    if (ids.empty()) {
        db.run(set + "topic LIKE ? ESCAPE '\\'", {visibility, me, me, likePrefix(topicPrefix)});
        countChanges();
    } else {
        for (int64_t id : ids) {
            db.run(set + "id = ?", {visibility, me, me, std::to_string(id)});
            countChanges();
        }
    }
    return changed;
}

std::string currentUserDirName() {
    std::string name;
    if (const char* env = std::getenv(kAuthorEnv); env && *env) {
        name = env;
    } else {
#ifdef _WIN32
        if (const wchar_t* w = _wgetenv(L"USERNAME"); w && *w) name = wideToUtf8(w);
#else
        if (const char* u = std::getenv("USER"); u && *u) name = u;
#endif
    }
    if (name.empty()) name = currentAuthor();
    for (char& c : name) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || std::string_view(kUnsafeDirChars).find(c) != std::string_view::npos) c = '_';
    }
    if (name.empty() || name == "." || name == "..") name = "user";
    return name;
}

std::filesystem::path rememberDir(const std::filesystem::path& root) {
    const auto shared = root / kRememberDir;
    std::error_code ec;
    const auto mine = shared / u8ToPath(currentUserDirName());
    if (std::filesystem::is_directory(mine, ec)) return mine;
    return shared;
}

void registerVisibilitySql(sqlite3* db) {
    if (!db) return;
    // Not SQLITE_DETERMINISTIC: the answer depends on the process author.
    sqlite3_create_function_v2(db, "icmg_can_see", 2, SQLITE_UTF8, nullptr,
                               &canSeeSqlFn, nullptr, nullptr, nullptr);
}

}  // namespace icmg::core

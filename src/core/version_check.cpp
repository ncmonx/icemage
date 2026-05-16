// Phase 85 Plan C: version staleness enforcement.
// Strategy: warn-only + nudge, not hard-block. Fail-open when offline.
#include "version_check.hpp"
#include "exec_utils.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <ctime>
#include <cstdlib>
#include <algorithm>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace icmg::core {

namespace {

fs::path cacheFilePath() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
    if (!home) home = std::getenv("HOMEPATH");
#else
    const char* home = std::getenv("HOME");
#endif
    fs::path base = home ? fs::path(home) : fs::temp_directory_path();
    return base / ".icmg_version_cache";
}

// Parse "MAJOR.MINOR.PATCH" → {major, minor, patch}. Returns {0,0,0} on fail.
std::tuple<int,int,int> parseSemver(const std::string& v) {
    int a = 0, b = 0, c = 0;
    char dot1 = 0, dot2 = 0;
    // Strip leading 'v'
    const char* s = v.c_str();
    if (*s == 'v' || *s == 'V') ++s;
    std::istringstream is(s);
    is >> a >> dot1 >> b >> dot2 >> c;
    return {a, b, c};
}

// Rough lag: count patch + minor*10 distance. Good enough for warn tiers.
int semverLag(const std::string& current, const std::string& latest) {
    auto [ca, cb, cc] = parseSemver(current);
    auto [la, lb, lc] = parseSemver(latest);
    if (la > ca) return 100; // major bump → always lag 10+
    if (la < ca) return 0;   // local is newer
    int delta = (lb - cb) * 10 + (lc - cc);
    return std::max(0, delta);
}

struct Cache { std::string latest; int64_t checked_at = 0; };

Cache readCache() {
    Cache c;
    std::ifstream f(cacheFilePath());
    if (!f) return c;
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (key == "latest")     c.latest     = val;
        if (key == "checked_at") try { c.checked_at = std::stoll(val); } catch (...) {}
    }
    return c;
}

void writeCache(const std::string& latest) {
    std::ofstream f(cacheFilePath());
    if (!f) return;
    f << "latest=" << latest << "\n"
      << "checked_at=" << (int64_t)std::time(nullptr) << "\n";
}

std::string fetchLatestTag(const std::string& repo, const std::string& current_version) {
    std::string url = "https://api.github.com/repos/" + repo + "/releases/latest";
    std::string cmd = "curl -sL --max-time 8 -H \"User-Agent: icmg/" + current_version
                    + "\" \"" + url + "\"";
    auto res = safeExecShell(cmd, false, 10000);
    if (res.exit_code != 0 || res.out.empty()) return {};
    // Minimal parse: find "tag_name":"v0.xx.y"
    auto& s = res.out;
    auto pos = s.find("\"tag_name\"");
    if (pos == std::string::npos) return {};
    pos = s.find('"', pos + 10);
    if (pos == std::string::npos) return {};
    ++pos;
    auto end = s.find('"', pos);
    if (end == std::string::npos) return {};
    std::string tag = s.substr(pos, end - pos);
    // Strip leading 'v'
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag = tag.substr(1);
    return tag;
}

} // anonymous namespace

// v1.3.0 (fixes #46): never block on the GitHub API call. Cache fresh →
// return cached. Cache stale or absent → spawn detached refresh thread and
// return the best-known answer immediately (cached if any, else unknown).
//
// Net effect: every command's hot path costs at most one tiny file read,
// not a 2–18s network round-trip on slow links.
//
// Opt-out: ICMG_NO_VERSION_CHECK=1 (handled by callers).
// Force foreground fetch: ICMG_VERSION_CHECK_SYNC=1 (debug / first install).
VersionStatus checkVersionStaleness(const std::string& current_version,
                                    const std::string& repo) {
    VersionStatus st;
    st.current    = current_version;
    st.online     = false;
    st.from_cache = false;
    st.lag        = 0;

    int64_t now = (int64_t)std::time(nullptr);
    constexpr int64_t CACHE_TTL   = 86400;      // 24h — skip network if fresh
    constexpr int64_t CACHE_STALE = 86400 * 7;  // 7d — give up and report unknown

    Cache cache = readCache();
    bool cache_fresh = !cache.latest.empty() && (now - cache.checked_at) < CACHE_TTL;
    bool cache_valid = !cache.latest.empty() && (now - cache.checked_at) < CACHE_STALE;

    if (cache_fresh) {
        st.latest     = cache.latest;
        st.from_cache = true;
        st.online     = true;
        st.lag        = semverLag(current_version, st.latest);
        return st;
    }

    // Sync path (debug / first-install verification) — only when explicitly
    // opted in via env. Default is async refresh below.
    bool sync = std::getenv("ICMG_VERSION_CHECK_SYNC") != nullptr;
    if (sync) {
        std::string fetched = fetchLatestTag(repo, current_version);
        if (!fetched.empty()) {
            st.latest = fetched;
            st.online = true;
            writeCache(fetched);
            st.lag = semverLag(current_version, st.latest);
            return st;
        }
        // sync fetch failed — fall through to cache-or-unknown below
    } else {
        // Spawn detached refresh — never blocks caller. Next invocation in
        // the next 24h gets the fresh value from cache.
        try {
            std::thread([repo, current_version]() {
                std::string fetched = fetchLatestTag(repo, current_version);
                if (!fetched.empty()) writeCache(fetched);
            }).detach();
        } catch (...) {
            // thread spawn failed — non-fatal, callers stay on cached or unknown
        }
    }

    if (cache_valid) {
        st.latest     = cache.latest;
        st.from_cache = true;
        st.online     = false; // stale cache served while refresh runs in bg
        st.lag        = semverLag(current_version, st.latest);
        return st;
    }

    st.lag = -1; // unknown — no usable cache yet; refresh thread will populate
    return st;
}

void printVersionWarning(const VersionStatus& status) {
    if (!status.online && status.lag < 0) return; // completely unknown — stay silent
    if (status.lag <= 2) return;                   // tiers 0-2: silent

    if (status.lag >= 10) {
        std::cerr << "[icmg] STALE VERSION: " << status.current
                  << " — latest is " << status.latest
                  << ". Some commands are disabled. Run: icmg upgrade\n";
    } else if (status.lag >= 5) {
        std::cerr << "[icmg] Version " << status.current << " is significantly outdated"
                  << " (latest: " << status.latest << "). Run: icmg upgrade\n";
    } else {
        // lag 3-4
        std::cerr << "[icmg] Update available: " << status.latest
                  << " (current: " << status.current << "). Run: icmg upgrade\n";
    }
}

bool isCommandSoftBlocked(const std::string& cmd, const VersionStatus& status) {
    if (status.lag < 10) return false;
    // Block re-init and graph scans — they may behave incorrectly on stale binaries.
    // Recall, search, backup, health always allowed.
    return cmd == "init" || cmd == "graph";
}

} // namespace icmg::core

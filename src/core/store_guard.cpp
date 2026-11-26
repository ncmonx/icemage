// #292: secret / PII filter on store. See store_guard.hpp.
#include "store_guard.hpp"
#include "config.hpp"
#include "path_u8.hpp"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <set>

namespace icmg::core {

namespace {

namespace fs = std::filesystem;

struct Rule {
    std::string type;
    std::regex  re;
    int         group = 0;          // capture group holding the secret (0 = whole match)
    bool        luhn  = false;      // card numbers: digits must pass the Luhn check
    bool        skipPlaceholder = false;
};

constexpr const char* kCustomType = "CUSTOM";
constexpr size_t kMinAssignedSecret = 6;

// Store-only rules (not in secret_scanner: `icmg scan` over source code would
// flag every `password = getenv(...)`; here placeholders are skipped).
const std::vector<Rule>& builtinRules() {
    static const std::vector<Rule> rules = [] {
        const auto icase = std::regex::ECMAScript | std::regex::icase;
        std::vector<Rule> v;
        v.push_back({"PASSWORD",
                     std::regex(R"((?:password|passwd|pwd|passphrase|secret|api[_-]?key|access[_-]?token|auth[_-]?token|client[_-]?secret|private[_-]?key)["']?\s*[:=]\s*["']?([^\s"'`,;]+))",
                                icase),
                     1, false, true});
        v.push_back({"URL_CREDENTIALS",
                     std::regex(R"([a-z][a-z0-9+.\-]*://[^\s:/@]+:([^\s@/]+)@)", icase), 1});
        v.push_back({"BEARER_TOKEN",
                     std::regex(R"(\bbearer\s+([A-Za-z0-9\-._~+/]{20,}=*))", icase), 1});
        v.push_back({"SSN", std::regex(R"(\b\d{3}-\d{2}-\d{4}\b)"), 0});
        v.push_back({"CARD_NUMBER",
                     std::regex(R"(\b(?:4\d{3}|5[1-5]\d{2}|2[2-7]\d{2}|6011|3[47]\d{2})(?:[ \-]?\d{3,4}){3}\b)"),
                     0, true});
        return v;
    }();
    return rules;
}

bool luhnOk(const std::string& s) {
    std::string d;
    for (char c : s) if (std::isdigit(static_cast<unsigned char>(c))) d += c;
    if (d.size() < 13 || d.size() > 19) return false;
    int sum = 0;
    bool dbl = false;
    for (auto it = d.rbegin(); it != d.rend(); ++it) {
        int v = *it - '0';
        if (dbl) { v *= 2; if (v > 9) v -= 9; }
        sum += v;
        dbl = !dbl;
    }
    return sum % 10 == 0;
}

// `password = <your-password>`, `${DB_PASS}`, `%TOKEN%`, `****`, `getenv(...)`.
bool looksLikePlaceholder(const std::string& v) {
    if (v.size() < kMinAssignedSecret) return true;
    const char c0 = v.front();
    if (c0 == '<' || c0 == '$' || c0 == '%' || c0 == '{' || c0 == '[' || c0 == '(') return true;
    if (v.find('(') != std::string::npos) return true;
    if (std::all_of(v.begin(), v.end(), [&](char c) { return c == c0; })) return true;
    std::string low(v);
    for (auto& c : low) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const char* p : {"xxx", "***", "redacted", "changeme", "example", "placeholder",
                          "your_", "your-", "env.", "process.", "os.environ", "null", "none",
                          "true", "false", "required", "optional"}) {
        if (low.find(p) != std::string::npos) return true;
    }
    return false;
}

// ---- user patterns ----

std::mutex g_user_mu;
bool g_user_override = false;                 // guarded by g_user_mu
std::vector<Rule> g_user_override_rules;      // guarded by g_user_mu
struct FileCache { std::string path; fs::file_time_type mtime{}; std::vector<Rule> rules; bool loaded = false; };
FileCache g_project_cache, g_env_cache;       // guarded by g_user_mu
std::set<std::string> g_bad_patterns_warned;  // guarded by g_user_mu

std::vector<Rule> parseRuleLines(const std::vector<std::string>& lines) {
    std::vector<Rule> out;
    for (auto line : lines) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos || line[b] == '#') continue;
        line = line.substr(b);
        std::string type = kCustomType;
        std::string pat = line;
        // NAME=regex where NAME is [A-Z0-9_]+ (so a regex containing '=' still works).
        const size_t eq = line.find('=');
        if (eq != std::string::npos && eq > 0) {
            const std::string head = line.substr(0, eq);
            if (std::all_of(head.begin(), head.end(), [](char c) {
                    return std::isupper(static_cast<unsigned char>(c)) ||
                           std::isdigit(static_cast<unsigned char>(c)) || c == '_';
                })) {
                type = head;
                pat = line.substr(eq + 1);
            }
        }
        if (pat.empty()) continue;
        try {
            out.push_back({type, std::regex(pat, std::regex::ECMAScript), 0});
        } catch (const std::regex_error&) {
            if (g_bad_patterns_warned.insert(pat).second)
                std::cerr << "[icmg] secret filter: skipping invalid pattern: " << pat << "\n";
        }
    }
    return out;
}

void refreshFile(FileCache& c, const std::string& path) {
    std::error_code ec;
    const fs::path p = u8ToPath(path);
    if (path.empty() || !fs::is_regular_file(p, ec)) { c = FileCache{}; return; }
    const auto mt = fs::last_write_time(p, ec);
    if (c.loaded && c.path == path && c.mtime == mt) return;
    std::vector<std::string> lines;
    std::ifstream in(p, std::ios::binary);
    for (std::string l; std::getline(in, l);) lines.push_back(l);
    c.path = path;
    c.mtime = mt;
    c.rules = parseRuleLines(lines);
    c.loaded = true;
}

std::vector<Rule> userRules() {
    std::lock_guard<std::mutex> lk(g_user_mu);
    if (g_user_override) return g_user_override_rules;
    refreshFile(g_project_cache, kSecretPatternsFile);
    const char* env = std::getenv(kSecretPatternsEnv);
    refreshFile(g_env_cache, env ? env : "");
    std::vector<Rule> all = g_project_cache.rules;
    all.insert(all.end(), g_env_cache.rules.begin(), g_env_cache.rules.end());
    return all;
}

void applyRule(const Rule& r, const std::string& text, std::vector<SecretMatch>* out) {
    for (auto it = std::sregex_iterator(text.begin(), text.end(), r.re); it != std::sregex_iterator(); ++it) {
        const auto& m = *it;
        const size_t g = (r.group > 0 && m.size() > static_cast<size_t>(r.group) && m[r.group].matched)
                             ? static_cast<size_t>(r.group) : 0;
        std::string val = m[g].str();
        if (val.empty()) continue;
        if (r.skipPlaceholder && looksLikePlaceholder(val)) continue;
        if (r.luhn && !luhnOk(val)) continue;
        out->push_back({r.type, val, static_cast<size_t>(m.position(g))});
    }
}

std::string lower(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return o;
}

// Shortest alnum token worth scrubbing from a sibling field (shorter ones like
// "ghp" or "sk" are prefixes, not the secret).
constexpr size_t kMinSecretToken = 4;
constexpr const char* kScrubbedToken = "REDACTED";

bool isTokChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

// Lowercased alnum tokens (>= kMinSecretToken) of every secret value.
std::set<std::string> secretTokens(const std::vector<std::vector<SecretMatch>>& perField) {
    std::set<std::string> toks;
    for (const auto& ms : perField)
        for (const auto& m : ms) {
            std::string cur;
            auto flush = [&] {
                if (cur.size() >= kMinSecretToken) toks.insert(lower(cur));
                cur.clear();
            };
            for (char c : m.match) { if (isTokChar(c)) cur += c; else flush(); }
            flush();
        }
    return toks;
}

// Replace every alnum token of `s` that is (case-insensitively) a secret token.
void scrubTokens(std::string& s, const std::set<std::string>& toks) {
    if (toks.empty()) return;
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        if (!isTokChar(s[i])) { out += s[i++]; continue; }
        size_t j = i;
        while (j < s.size() && isTokChar(s[j])) ++j;
        const std::string tok = s.substr(i, j - i);
        out += toks.count(lower(tok)) ? std::string(kScrubbedToken) : tok;
        i = j;
    }
    s = std::move(out);
}

}  // namespace

std::string keywordSourceText(const std::string& text) {
    const auto matches = scanForStore(text);
    if (matches.empty()) return text;
    std::string out;
    size_t pos = 0;
    for (const auto& m : matches) {
        if (m.offset < pos) continue;
        out.append(text, pos, m.offset - pos);
        out += ' ';
        pos = m.offset + m.match.size();
    }
    out.append(text, pos, std::string::npos);
    return out;
}

SecretMode parseSecretMode(std::string_view s, bool* ok) {
    const std::string v = lower(s);
    if (ok) *ok = true;
    if (v == "reject") return SecretMode::Reject;
    if (v == "mask")   return SecretMode::Mask;
    if (v == "warn")   return SecretMode::Warn;
    if (v == "off" || v == "0" || v == "none") return SecretMode::Off;
    if (ok) *ok = false;
    return kDefaultSecretMode;
}

const char* secretModeName(SecretMode m) {
    switch (m) {
        case SecretMode::Reject: return "reject";
        case SecretMode::Mask:   return "mask";
        case SecretMode::Warn:   return "warn";
        case SecretMode::Off:    return "off";
    }
    return "mask";
}

SecretMode activeSecretMode() {
    bool ok = false;
    if (const char* env = std::getenv(kSecretModeEnv); env && *env) {
        SecretMode m = parseSecretMode(env, &ok);
        if (ok) return m;
    }
    try {
        const std::string cfg = Config::instance().getString(kSecretModeConfig);
        if (!cfg.empty()) {
            SecretMode m = parseSecretMode(cfg, &ok);
            if (ok) return m;
        }
    } catch (...) {}
    return kDefaultSecretMode;
}

std::vector<SecretMatch> scanForStore(const std::string& text) {
    std::vector<SecretMatch> out = scanSecrets(text);
    for (const auto& r : builtinRules()) applyRule(r, text, &out);
    for (const auto& r : userRules()) applyRule(r, text, &out);
    std::sort(out.begin(), out.end(), [](const SecretMatch& a, const SecretMatch& b) {
        return a.offset != b.offset ? a.offset < b.offset : a.match.size() > b.match.size();
    });
    // Drop matches nested inside an earlier, longer one (same secret, two rules).
    std::vector<SecretMatch> dedup;
    size_t end = 0;
    for (auto& m : out) {
        if (!dedup.empty() && m.offset < end) continue;
        end = m.offset + m.match.size();
        dedup.push_back(std::move(m));
    }
    return dedup;
}

std::vector<std::string> guardFields(const std::vector<std::string*>& fields,
                                     SecretMode mode, const char* what) {
    std::vector<std::string> types;
    if (mode == SecretMode::Off) return types;
    std::vector<std::vector<SecretMatch>> perField;
    perField.reserve(fields.size());
    for (auto* f : fields) {
        perField.push_back(f ? scanForStore(*f) : std::vector<SecretMatch>{});
        for (const auto& m : perField.back())
            if (std::find(types.begin(), types.end(), m.type) == types.end()) types.push_back(m.type);
    }
    if (types.empty()) return types;

    std::string list;
    for (const auto& t : types) list += (list.empty() ? "" : ",") + t;
    if (mode == SecretMode::Reject) {
        throw SecretRejectedError(std::string("refused: text looks like it holds a secret (") + list +
                                  "). Remove it, or set " + kSecretModeEnv + "=mask|warn|off");
    }
    if (mode == SecretMode::Mask) {
        for (size_t i = 0; i < fields.size(); ++i)
            if (fields[i] && !perField[i].empty()) *fields[i] = redactSecrets(*fields[i], perField[i]);
        // #294: derived sibling fields (auto keywords, tags) lost the pattern
        // context, so scrub the secret's own tokens from every field.
        const auto toks = secretTokens(perField);
        for (auto* f : fields) if (f) scrubTokens(*f, toks);
        std::cerr << "[icmg " << what << "] masked possible secret(s) before storing: " << list
                  << " (set " << kSecretModeEnv << "=reject|warn|off to change)\n";
    } else {
        std::cerr << "[icmg " << what << "] warning: text may hold secret(s): " << list
                  << " (stored as-is; " << kSecretModeEnv << "=warn)\n";
    }
    return types;
}

std::vector<std::string> guardFields(const std::vector<std::string*>& fields, const char* what) {
    return guardFields(fields, activeSecretMode(), what);
}

void setUserSecretPatternsForTest(const std::vector<std::string>& lines) {
    std::lock_guard<std::mutex> lk(g_user_mu);
    g_user_override = !lines.empty();
    g_user_override_rules.clear();
    if (g_user_override) {
        // parseRuleLines touches g_bad_patterns_warned: already under the lock.
        g_user_override_rules = parseRuleLines(lines);
    }
}

}  // namespace icmg::core

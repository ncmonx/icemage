#include "legacy_text_repair.hpp"
#include "db.hpp"
#include "atomic_file.hpp"  // isValidUtf8 (strict)

#include <string>
#include <vector>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include "win_utf8.hpp"
#endif

namespace icmg::core {

namespace {
constexpr unsigned kUtf8CodePage = 65001;
constexpr const char* kMarkerTable = "icmg_text_repair";

std::string quoteIdent(const std::string& name) {
    std::string out = "\"";
    for (char c : name) { out += c; if (c == '"') out += '"'; }
    return out + "\"";
}

bool hasHighByte(const std::string& s) {
    for (unsigned char c : s) if (c >= 0x80) return true;
    return false;
}

std::string scalar(Db& db, const std::string& sql) {
    std::string out;
    db.query(sql, {}, [&](const Row& r) { if (!r.empty()) out = r[0]; });
    return out;
}

// Ordinary tables only: no sqlite_* internals, no marker, no virtual tables
// and no shadow tables of a virtual table (named "<vtab>_...").
std::vector<std::string> repairableTables(Db& db) {
    std::vector<std::string> virt, plain;
    db.query("SELECT name, COALESCE(sql,'') FROM sqlite_master WHERE type='table' "
             "AND name NOT LIKE 'sqlite\\_%' ESCAPE '\\'", {},
             [&](const Row& r) {
                 if (r.size() < 2 || r[0] == kMarkerTable) return;
                 const bool isVirtual = r[1].rfind("CREATE VIRTUAL", 0) == 0;
                 (isVirtual ? virt : plain).push_back(r[0]);
             });
    std::vector<std::string> out;
    for (const auto& t : plain) {
        bool shadow = false;
        for (const auto& v : virt)
            if (t.size() > v.size() && t.compare(0, v.size() + 1, v + "_") == 0) shadow = true;
        if (!shadow) out.push_back(t);
    }
    return out;
}

std::vector<std::string> columnsOf(Db& db, const std::string& table) {
    std::vector<std::string> cols;
    db.query("SELECT name FROM pragma_table_info(?)", {table},
             [&](const Row& r) { if (!r.empty()) cols.push_back(r[0]); });
    return cols;
}

int repairColumn(Db& db, const std::string& table, const std::string& col, unsigned cp) {
    const std::string qt = quoteIdent(table), qc = quoteIdent(col);
    std::vector<std::string> bad;
    db.query("SELECT DISTINCT " + qc + " FROM " + qt + " WHERE typeof(" + qc + ")='text'", {},
             [&](const Row& r) {
                 if (!r.empty() && hasHighByte(r[0]) && !isValidUtf8(r[0])) bad.push_back(r[0]);
             });
    int changed = 0;
    for (const auto& v : bad) {
        const std::string fixed = legacyAnsiToUtf8(v, cp);
        if (fixed == v) continue;
        // OR IGNORE: a UNIQUE clash with an existing UTF-8 twin keeps the old
        // row rather than deleting the twin; triggers still fire on updates.
        db.run("UPDATE OR IGNORE " + qt + " SET " + qc + " = ? WHERE " + qc + " = ?", {fixed, v});
        changed += std::stoi("0" + scalar(db, "SELECT changes()"));
    }
    return changed;
}
}  // namespace

std::string legacyAnsiToUtf8(const std::string& s, unsigned codePage) {
#ifdef _WIN32
    if (s.empty() || codePage == 0) return s;
    const int wn = MultiByteToWideChar(codePage, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (wn <= 0) return s;
    std::wstring w(static_cast<size_t>(wn), L'\0');
    MultiByteToWideChar(codePage, 0, s.data(), static_cast<int>(s.size()), w.data(), wn);
    return wideToUtf8(w);
#else
    (void)codePage;
    return s;
#endif
}

unsigned systemAnsiCodePage() {
#ifdef _WIN32
    // GetACP() reports THIS process's code page (UTF-8 via the manifest), so
    // read the system setting the pre-manifest builds actually ran under.
    wchar_t buf[16] = {};
    DWORD sz = sizeof(buf);
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage",
                     L"ACP", RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS) {
        const unsigned cp = static_cast<unsigned>(wcstoul(buf, nullptr, 10));
        if (cp != 0) return cp;
    }
    DWORD cp = 0;
    if (GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                        reinterpret_cast<LPWSTR>(&cp), sizeof(cp) / sizeof(wchar_t)) > 0)
        return cp;
    return 0;
#else
    return 0;
#endif
}

int repairLegacyAnsiText(Db& db, unsigned codePage) {
    if (codePage == 0 || codePage == kUtf8CodePage) return 0;
    try {
        db.run(std::string("CREATE TABLE IF NOT EXISTS ") + kMarkerTable +
               "(id INTEGER PRIMARY KEY CHECK (id = 1), code_page INTEGER,"
               " rows_fixed INTEGER, done_at INTEGER)");
        if (!scalar(db, std::string("SELECT 1 FROM ") + kMarkerTable + " WHERE id = 1").empty())
            return 0;
    } catch (...) {
        return 0;  // read-only DB etc.: try again next start
    }

    int fixed = 0;
    try {
        db.run("BEGIN IMMEDIATE");
        try {
            for (const auto& t : repairableTables(db))
                for (const auto& c : columnsOf(db, t)) fixed += repairColumn(db, t, c, codePage);
            db.run(std::string("INSERT OR IGNORE INTO ") + kMarkerTable +
                   "(id, code_page, rows_fixed, done_at) VALUES (1, ?, ?, strftime('%s','now'))",
                   {std::to_string(codePage), std::to_string(fixed)});
            db.run("COMMIT");
        } catch (...) {
            db.run("ROLLBACK");
            return 0;
        }
    } catch (...) {
        return 0;
    }
    return fixed;
}

}  // namespace icmg::core

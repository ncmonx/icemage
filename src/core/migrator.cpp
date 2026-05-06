#include "migrator.hpp"
#include "embedded_migrations.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>
#include <stdexcept>
#include <iostream>

namespace fs = std::filesystem;
namespace icmg::core {

Migrator::Migrator(const std::string& migrations_dir)
    : dir_(migrations_dir) {}

std::vector<Migrator::Migration> Migrator::discover() const {
    std::vector<Migration> result;

    if (!fs::exists(dir_)) return result;

    std::regex pattern(R"((\d{4})_.+\.sql)");
    for (auto& entry : fs::directory_iterator(dir_)) {
        if (!entry.is_regular_file()) continue;
        std::string name = entry.path().filename().string();
        std::smatch m;
        if (std::regex_match(name, m, pattern)) {
            int ver = std::stoi(m[1].str());
            result.push_back({ver, entry.path().string()});
        }
    }

    std::sort(result.begin(), result.end(),
        [](const Migration& a, const Migration& b) { return a.version < b.version; });

    return result;
}

int Migrator::latestVersion() const {
    auto migs = discover();
    if (migs.empty()) return 0;
    return migs.back().version;
}

// Strip standalone BEGIN/COMMIT/ROLLBACK lines from SQL so migration files
// can optionally include them for readability without breaking nested-txn.
static std::string stripTransactionStatements(const std::string& sql) {
    std::istringstream in(sql);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        // Trim leading whitespace for comparison
        std::string trimmed = line;
        auto it = trimmed.find_first_not_of(" \t\r");
        if (it != std::string::npos) trimmed = trimmed.substr(it);
        // Convert to uppercase for comparison
        std::string upper = trimmed;
        for (auto& c : upper) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        // Strip BEGIN[;] COMMIT[;] ROLLBACK[;] lines
        if (upper == "BEGIN" || upper == "BEGIN;" ||
            upper == "BEGIN TRANSACTION" || upper == "BEGIN TRANSACTION;" ||
            upper == "COMMIT" || upper == "COMMIT;" ||
            upper == "ROLLBACK" || upper == "ROLLBACK;") {
            continue; // skip this line
        }
        out << line << '\n';
    }
    return out.str();
}

void Migrator::apply(Db& db, const Migration& m) {
    // Read SQL file
    std::ifstream f(m.path);
    if (!f) throw std::runtime_error("Migration file not found: " + m.path);
    std::string raw((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());

    // Strip BEGIN/COMMIT from file — migrator owns the transaction
    std::string sql = stripTransactionStatements(raw);

    db.run("BEGIN TRANSACTION");
    try {
        db.run(sql);
        db.run("COMMIT");
        db.setUserVersion(m.version);
        std::cerr << "[icmg] applied migration " << m.version
                  << " (" << fs::path(m.path).filename().string() << ")\n";
    } catch (...) {
        db.run("ROLLBACK");
        throw;
    }
}

void Migrator::runAll(Db& db) {
    int current = db.userVersion();
    auto migs   = discover();

    if (!migs.empty()) {
        // File-based migrations (dev mode, running from repo root)
        for (auto& m : migs) {
            if (m.version <= current) continue;
            apply(db, m);
        }
    } else {
        // Embedded migrations fallback (binary deployed away from repo)
        for (auto& [ver, sql] : embeddedMigrations()) {
            if (ver <= current) continue;
            db.run("BEGIN TRANSACTION");
            try {
                db.run(stripTransactionStatements(sql));
                db.run("COMMIT");
                db.setUserVersion(ver);
            } catch (...) {
                db.run("ROLLBACK");
                throw;
            }
        }
    }
}

} // namespace icmg::core

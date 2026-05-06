#include "migrator.hpp"
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

void Migrator::apply(Db& db, const Migration& m) {
    // Read SQL file
    std::ifstream f(m.path);
    if (!f) throw std::runtime_error("Migration file not found: " + m.path);
    std::string sql((std::istreambuf_iterator<char>(f)),
                     std::istreambuf_iterator<char>());

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

    for (auto& m : migs) {
        if (m.version <= current) continue;
        apply(db, m);
    }
}

} // namespace icmg::core

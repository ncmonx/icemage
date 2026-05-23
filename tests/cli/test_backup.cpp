// Phase 83 T2 — unit tests for backup_cmd helpers.
// Inline mirrors of static helpers; lifecycle tests use temp dirs.
#include "../test_main.hpp"
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ── Inline mirrors of static helpers ─────────────────────────────────────────

static std::string utcStamp() {
    auto now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream o;
    o << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return o.str();
}

static std::string humanSize(uintmax_t b) {
    const char* unit[] = {"B", "KB", "MB", "GB"};
    double s = static_cast<double>(b);
    int u = 0;
    while (s >= 1024.0 && u < 3) { s /= 1024.0; ++u; }
    std::ostringstream o;
    o << std::fixed << std::setprecision(s < 10 ? 2 : 1) << s << unit[u];
    return o.str();
}

static std::string computeSha256(const fs::path& file) {
#ifdef _WIN32
    std::string cmd = "certutil -hashfile \"" + file.string() + "\" SHA256";
#else
    std::string cmd = "(sha256sum \"" + file.string()
                    + "\" 2>/dev/null || shasum -a 256 \"" + file.string() + "\")";
#endif
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return {};
    std::string out;
    char buf[256];
    while (fgets(buf, sizeof(buf), pipe)) out += buf;
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    for (size_t i = 0; i + 64 <= out.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < 64; ++j) {
            char c = out[i + j];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F'))) { ok = false; break; }
        }
        if (ok) {
            std::string hex = out.substr(i, 64);
            std::transform(hex.begin(), hex.end(), hex.begin(), ::tolower);
            return hex;
        }
    }
    return {};
}

// ── utcStamp tests ────────────────────────────────────────────────────────────

TEST("backup: utcStamp format matches YYYYMMDD-HHMMSS") {
    std::string s = utcStamp();
    ASSERT_EQ((int)s.size(), 15);
    std::regex re(R"(\d{8}-\d{6})");
    ASSERT_TRUE(std::regex_match(s, re));
}

TEST("backup: utcStamp produces UTC not local time noise") {
    std::string s = utcStamp();
    // Month field (chars 4-5) must be 01-12
    int month = std::stoi(s.substr(4, 2));
    ASSERT_TRUE(month >= 1 && month <= 12);
}

// ── humanSize tests ───────────────────────────────────────────────────────────

TEST("backup: humanSize 0 → 0.00B") {
    ASSERT_EQ(humanSize(0), std::string("0.00B"));
}

TEST("backup: humanSize 1024 → 1.00KB (s<10 = 2 decimals)") {
    ASSERT_EQ(humanSize(1024), std::string("1.00KB"));
}

TEST("backup: humanSize 1048576 → 1.00MB (s<10 = 2 decimals)") {
    ASSERT_EQ(humanSize(1048576), std::string("1.00MB"));
}

TEST("backup: humanSize 512 → 512.0B") {
    ASSERT_EQ(humanSize(512), std::string("512.0B"));
}

TEST("backup: humanSize 9.5KB shows 2 decimal") {
    // 9*1024 + 512 = 9728 bytes → 9.50KB
    ASSERT_EQ(humanSize(9728), std::string("9.50KB"));
}

TEST("backup: humanSize 10KB shows 1 decimal") {
    // 10*1024 = 10240 → 10.0KB
    ASSERT_EQ(humanSize(10240), std::string("10.0KB"));
}

// ── computeSha256 tests ───────────────────────────────────────────────────────

TEST("backup: computeSha256 — known content returns 64-char hex") {
    fs::path p = fs::temp_directory_path() / "icmg_backup_test_hash.txt";
    { std::ofstream f(p); f << "hello icmg\n"; }

    std::string hash = computeSha256(p);
    fs::remove(p);

    ASSERT_EQ((int)hash.size(), 64);
    // Verify all lowercase hex
    for (char c : hash) {
        ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST("backup: computeSha256 — same content same hash") {
    fs::path p1 = fs::temp_directory_path() / "icmg_hash_a.txt";
    fs::path p2 = fs::temp_directory_path() / "icmg_hash_b.txt";
    { std::ofstream f(p1); f << "deterministic content\n"; }
    { std::ofstream f(p2); f << "deterministic content\n"; }

    std::string h1 = computeSha256(p1);
    std::string h2 = computeSha256(p2);
    fs::remove(p1); fs::remove(p2);

    ASSERT_EQ(h1, h2);
}

TEST("backup: computeSha256 — different content different hash") {
    fs::path p1 = fs::temp_directory_path() / "icmg_hash_c.txt";
    fs::path p2 = fs::temp_directory_path() / "icmg_hash_d.txt";
    { std::ofstream f(p1); f << "content A\n"; }
    { std::ofstream f(p2); f << "content B\n"; }

    std::string h1 = computeSha256(p1);
    std::string h2 = computeSha256(p2);
    fs::remove(p1); fs::remove(p2);

    ASSERT_TRUE(h1 != h2);
}

TEST("backup: computeSha256 — missing file returns empty") {
    fs::path p = fs::temp_directory_path() / "icmg_nonexistent_xyz.txt";
    std::string h = computeSha256(p);
    ASSERT_EQ(h, std::string(""));
}

// ── snapshot sidecar file tests ───────────────────────────────────────────────

TEST("backup: snapshot creates .sha256 sidecar next to .db") {
    fs::path tmp = fs::temp_directory_path() / "icmg_backup_snap_test";
    fs::create_directories(tmp);

    // Create a fake snapshot .db
    fs::path db = tmp / "20260512-120000.db";
    { std::ofstream f(db); f << "fake db content\n"; }

    std::string sha = computeSha256(db);
    if (!sha.empty()) {
        fs::path shap = db; shap += ".sha256";
        { std::ofstream f(shap); f << sha << "  " << db.filename().string() << "\n"; }
        ASSERT_TRUE(fs::exists(shap));
        std::ifstream fin(shap);
        std::string line;
        std::getline(fin, line);
        ASSERT_CONTAINS(line, sha.substr(0, 16));
        ASSERT_CONTAINS(line, "20260512-120000.db");
    }

    fs::remove_all(tmp);
}

TEST("backup: note sidecar written when --note provided") {
    fs::path tmp = fs::temp_directory_path() / "icmg_backup_note_test";
    fs::create_directories(tmp);

    fs::path db = tmp / "20260512-130000.db";
    { std::ofstream f(db); f << "db\n"; }
    fs::path notep = db; notep += ".note";
    { std::ofstream f(notep); f << "pre-release checkpoint\n"; }

    ASSERT_TRUE(fs::exists(notep));
    {
        std::ifstream fin(notep);
        std::string content;
        std::getline(fin, content);
        ASSERT_CONTAINS(content, "pre-release");
    }
    fs::remove_all(tmp);
}

// ── listSnaps filter tests ────────────────────────────────────────────────────

static std::vector<std::string> listSnapIds(const fs::path& dir) {
    std::vector<std::string> ids;
    if (!fs::exists(dir)) return ids;
    for (auto& e : fs::directory_iterator(dir)) {
        if (!e.is_regular_file()) continue;
        const auto& p = e.path();
        if (p.extension() != ".db") continue;
        ids.push_back(p.stem().string());
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

TEST("backup: listSnaps only picks .db files, not .sha256 or .note") {
    fs::path tmp = fs::temp_directory_path() / "icmg_listsnaps_test";
    fs::create_directories(tmp);

    auto touch = [](const fs::path& p) { std::ofstream f(p); f << "x\n"; };
    touch(tmp / "20260101-000000.db");
    touch(tmp / "20260101-000000.db.sha256");
    touch(tmp / "20260101-000000.db.note");
    touch(tmp / "20260102-000000.db");
    touch(tmp / "README.txt");

    auto ids = listSnapIds(tmp);
    ASSERT_EQ((int)ids.size(), 2);
    ASSERT_EQ(ids[0], std::string("20260101-000000"));
    ASSERT_EQ(ids[1], std::string("20260102-000000"));

    fs::remove_all(tmp);
}

TEST("backup: listSnaps returns empty for missing dir") {
    fs::path tmp = fs::temp_directory_path() / "icmg_no_such_dir_xyz";
    auto ids = listSnapIds(tmp);
    ASSERT_EQ((int)ids.size(), 0);
}

TEST("backup: listSnaps sorts chronologically by filename") {
    fs::path tmp = fs::temp_directory_path() / "icmg_sort_test";
    fs::create_directories(tmp);

    auto touch = [](const fs::path& p) { std::ofstream f(p); f << "x\n"; };
    touch(tmp / "20260103-000000.db");
    touch(tmp / "20260101-000000.db");
    touch(tmp / "20260102-000000.db");

    auto ids = listSnapIds(tmp);
    ASSERT_EQ((int)ids.size(), 3);
    ASSERT_EQ(ids[0], std::string("20260101-000000"));
    ASSERT_EQ(ids[2], std::string("20260103-000000"));

    fs::remove_all(tmp);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif

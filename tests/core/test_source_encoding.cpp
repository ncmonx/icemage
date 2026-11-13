// Issue #277 root cause: MSVC (no /utf-8 flag) decodes a source file that
// starts with a UTF-8 BOM as UTF-8 and then narrows every string literal to
// the ANSI codepage, so "\u2014" became byte 0x97 in the binary. BOM-less
// sources round-trip their UTF-8 bytes unchanged. Keep src/ BOM-free.
#include "../test_main.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {
// Repo root derived from this file's own path (tests/core/<file>), so the
// check needs no build-system define. Empty when sources are not on disk.
fs::path repoRoot() {
    fs::path self(__FILE__);
    if (!self.is_absolute()) return {};
    fs::path root = self.parent_path().parent_path().parent_path();
    return fs::exists(root / "src") ? root : fs::path{};
}

bool hasBom(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    char b[3] = {0, 0, 0};
    f.read(b, 3);
    return f.gcount() == 3 && static_cast<unsigned char>(b[0]) == 0xEF &&
           static_cast<unsigned char>(b[1]) == 0xBB && static_cast<unsigned char>(b[2]) == 0xBF;
}

bool isCxxSource(const fs::path& p) {
    const auto ext = p.extension().string();
    return ext == ".cpp" || ext == ".hpp" || ext == ".h" || ext == ".c" || ext == ".cc";
}
}  // namespace

TEST("source encoding: no C/C++ file under src/ starts with a UTF-8 BOM") {
    const fs::path root = repoRoot();
    if (root.empty()) return;  // sources not available (packaged test run)
    std::vector<std::string> offenders;
    for (auto& e : fs::recursive_directory_iterator(root / "src")) {
        if (e.is_regular_file() && isCxxSource(e.path()) && hasBom(e.path()))
            offenders.push_back(fs::relative(e.path(), root).generic_string());
    }
    for (auto& o : offenders) std::cerr << "         BOM: " << o << "\n";
    ASSERT_TRUE(offenders.empty());
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif

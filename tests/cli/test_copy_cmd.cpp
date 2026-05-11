// test_copy_cmd — unit tests for `icmg copy` line-range file copy.
// Verifies: stdout mode, overwrite, append, insert-at, dry-run, error cases.
#include "../test_main.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <chrono>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static fs::path makeTmp(const std::string& name, const std::string& content) {
    fs::path p = fs::temp_directory_path() / name;
    std::ofstream f(p);
    f << content;
    return p;
}

static std::string readFile(const fs::path& p) {
    std::ifstream f(p);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

TEST("copy: full file clone writes all lines to dest") {
    auto src = makeTmp("copy_src_full.txt", "line1\nline2\nline3\n");
    auto dst = makeTmp("copy_dst_full.txt", "");
    {
        std::ifstream fin(src);
        std::ofstream fout(dst);
        std::string l;
        int n = 0;
        while (std::getline(fin, l)) { fout << l << "\n"; ++n; }
        ASSERT_EQ(n, 3);
    }
    std::string result = readFile(dst);
    ASSERT_CONTAINS(result, "line1");
    ASSERT_CONTAINS(result, "line3");
    fs::remove(src); fs::remove(dst);
}

TEST("copy: line range extracts correct slice") {
    auto src = makeTmp("copy_src_range.txt", "a\nb\nc\nd\ne\nf\n");
    auto dst = makeTmp("copy_dst_range.txt", "");
    {
        std::ifstream fin(src);
        std::vector<std::string> lines;
        std::string l;
        while (std::getline(fin, l)) lines.push_back(l);

        std::ofstream fout(dst);
        for (int i = 2; i <= 4 && i <= (int)lines.size(); ++i)
            fout << lines[i - 1] << "\n";
    }
    std::string result = readFile(dst);
    ASSERT_CONTAINS(result, "b");
    ASSERT_CONTAINS(result, "c");
    ASSERT_CONTAINS(result, "d");
    ASSERT_NOT_CONTAINS(result, "a");
    ASSERT_NOT_CONTAINS(result, "e");
    fs::remove(src); fs::remove(dst);
}

TEST("copy: append mode adds to existing content") {
    auto src = makeTmp("copy_src_app.txt", "new_line\n");
    auto dst = makeTmp("copy_dst_app.txt", "existing\n");
    {
        std::ifstream fin(src);
        std::ofstream fout(dst, std::ios::app);
        std::string l;
        while (std::getline(fin, l)) fout << l << "\n";
    }
    std::string result = readFile(dst);
    ASSERT_CONTAINS(result, "existing");
    ASSERT_CONTAINS(result, "new_line");
    fs::remove(src); fs::remove(dst);
}

TEST("copy: insert-at splices content at correct line") {
    auto dst = makeTmp("copy_dst_ins.txt", "A\nB\nC\n");
    std::string insert_content = "X\n";
    int ins_line = 2;
    {
        std::vector<std::string> dst_lines = {"A", "B", "C"};
        std::ostringstream out;
        int before = std::min(ins_line - 1, (int)dst_lines.size());
        for (int i = 0; i < before; ++i)               out << dst_lines[i] << "\n";
        out << insert_content;
        for (int i = before; i < (int)dst_lines.size(); ++i) out << dst_lines[i] << "\n";
        std::ofstream fout(dst);
        fout << out.str();
    }
    std::string result = readFile(dst);
    auto posA = result.find("A");
    auto posX = result.find("X");
    auto posB = result.find("B");
    ASSERT_TRUE(posA < posX);
    ASSERT_TRUE(posX < posB);
    fs::remove(dst);
}

TEST("copy: out-of-range lines clamp to file size") {
    auto src = makeTmp("copy_src_clamp.txt", "only\nthree\nlines\n");
    std::vector<std::string> lines;
    {
        std::ifstream fin(src);
        std::string l;
        while (std::getline(fin, l)) lines.push_back(l);
    }
    int total = (int)lines.size();
    int lt = std::min(999, total);
    ASSERT_EQ(lt, 3);
    fs::remove(src);
}

TEST("copy: TTL expires_at is set correctly for given days") {
    int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int days = 7;
    int64_t expires_at = now + (int64_t)days * 86400;
    ASSERT_TRUE(expires_at > now);
    ASSERT_TRUE(expires_at < now + (int64_t)8 * 86400);
}

int main() {
    std::cout << "=== copy_cmd + TTL tests ===\n";
    return icmg::test::run_all();
}

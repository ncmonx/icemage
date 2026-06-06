// 2026-06-06: durable comms archive round-trip + never-truncate. #comms
#include "../test_main.hpp"
#include "../../src/core/comms_archive.hpp"
#include <filesystem>

using namespace icmg::core;

TEST("comms_archive: append + read round-trip, never truncates") {
    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path() / "icmg_comms_test.jsonl";
    std::error_code ec; fs::remove(p, ec);
    commsAppend(p.string(), "claudy", "luna", "hai");
    commsAppend(p.string(), "luna", "claudy", "hai balik");
    auto rows = commsRead(p.string());
    ASSERT_EQ(rows.size(), (size_t)2);
    ASSERT_EQ(rows[0].from, std::string("claudy"));
    ASSERT_EQ(rows[1].body, std::string("hai balik"));
    fs::remove(p, ec);
}

TEST("comms_archive: escapes quotes + newlines safely") {
    namespace fs = std::filesystem;
    fs::path p = fs::temp_directory_path() / "icmg_comms_esc_test.jsonl";
    std::error_code ec; fs::remove(p, ec);
    commsAppend(p.string(), "a", "b", "line1\nline2 \"quoted\"");
    auto rows = commsRead(p.string());
    ASSERT_EQ(rows.size(), (size_t)1);
    ASSERT_EQ(rows[0].body, std::string("line1\nline2 \"quoted\""));
    fs::remove(p, ec);
}

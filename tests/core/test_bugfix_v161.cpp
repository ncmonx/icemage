// v1.61 bug-fixes from field (other server):
//   Bug 1: capOutput spill must not crash on over-cap output (Win temp-dir
//          filesystem_error "module could not be found").
//   Bug 2: sync push json::dump must not throw on non-UTF-8 (PNG 0x89) bytes.

#include "../test_main.hpp"
#include "../../src/core/output_cap.hpp"
#include <nlohmann/json.hpp>
#include <string>

using namespace icmg::core;

// ---- Bug 1: capOutput ----

TEST("capOutput: under cap returns full unchanged (no spill)") {
    std::string spill;
    std::string in = "short output";
    auto out = capOutput(in, 4096, spill);
    ASSERT_EQ(out, in);
    ASSERT_TRUE(spill.empty());
}

TEST("capOutput: over cap returns truncated string with footer, never throws") {
    std::string spill;
    std::string big(20000, 'x');
    std::string out;
    // The key contract: this call must NOT throw even if spill fails.
    bool threw = false;
    try { out = capOutput(big, 4096, spill); }
    catch (...) { threw = true; }
    ASSERT_FALSE(threw);
    ASSERT_TRUE(out.size() < big.size());
    ASSERT_TRUE(out.find("truncated") != std::string::npos);
}

TEST("capOutput: over cap keeps head + tail") {
    std::string spill;
    std::string in = std::string(5000, 'A') + "TAILMARKER";
    auto out = capOutput(in, 1000, spill);
    ASSERT_TRUE(out.find("AAAA") != std::string::npos);  // head present
    ASSERT_TRUE(out.find("TAILMARKER") != std::string::npos);  // tail preserved
}

// ---- Bug 2: json dump with non-UTF-8 ----

TEST("sync dump: non-UTF-8 byte does not throw with replace handler") {
    nlohmann::json row;
    // 0x89 is the PNG magic byte — invalid as a lone UTF-8 lead byte.
    std::string binary;
    binary.push_back('\x89');
    binary += "PNG\r\n";
    row["topic"] = "image-blob";
    row["content"] = binary;

    bool threw = false;
    std::string wire;
    try {
        wire = row.dump(-1, ' ', false,
                        nlohmann::json::error_handler_t::replace);
    } catch (...) { threw = true; }
    ASSERT_FALSE(threw);
    ASSERT_TRUE(!wire.empty());
    ASSERT_TRUE(wire.find("image-blob") != std::string::npos);
}

TEST("sync dump: default handler DOES throw (confirms the bug + need for fix)") {
    nlohmann::json row;
    std::string binary; binary.push_back('\x89');
    row["content"] = binary;
    bool threw = false;
    try { (void)row.dump(); } catch (...) { threw = true; }
    ASSERT_TRUE(threw);   // proves replace handler is required
}

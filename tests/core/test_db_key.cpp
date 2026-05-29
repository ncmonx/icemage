// v1.76 T2+T3: db_key — hex codec, config parse, key-mode resolve, key gen, DPAPI.
#include "../test_main.hpp"
#include "../../src/core/db_key.hpp"
using namespace icmg::core;

TEST("db_key: hex roundtrip") {
    std::vector<unsigned char> raw = {0x00,0x11,0xab,0xff};
    ASSERT_EQ(toHex(raw), std::string("0011abff"));
    auto back = fromHex("0011abff");
    ASSERT_TRUE(back == raw);
}

TEST("db_key: keyMode priority env > explicit > default dpapi") {
    EncryptionConfig c; c.enabled = true; c.key_mode = "dpapi";
    ASSERT_EQ(resolveKeyMode(c, true),  std::string("env"));
    ASSERT_EQ(resolveKeyMode(c, false), std::string("dpapi"));
    EncryptionConfig s; s.enabled = true; s.key_mode = "shared";
    ASSERT_EQ(resolveKeyMode(s, false), std::string("shared"));
}

TEST("db_key: config parse round-trips enabled/scope/mode") {
    auto c = parseEncryptionConfig(R"({"enabled":true,"scope":"project","key_mode":"shared"})");
    ASSERT_TRUE(c.enabled);
    ASSERT_EQ(c.scope, std::string("project"));
    ASSERT_EQ(c.key_mode, std::string("shared"));
}

TEST("db_key: disabled config when file absent/empty") {
    ASSERT_FALSE(parseEncryptionConfig("").enabled);
}

TEST("db_key: generateDbKey is 64 hex chars (256-bit)") {
    auto k = generateDbKey();
    ASSERT_EQ((int)k.size(), 64);
    for (char ch : k) ASSERT_TRUE((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f'));
}

#ifdef _WIN32
TEST("db_key: DPAPI wrap then unwrap roundtrips") {
    std::string secret = "deadbeefcafebabe";
    auto blob = dpapiWrap(secret);
    ASSERT_FALSE(blob.empty());
    ASSERT_EQ(dpapiUnwrap(blob), secret);
}
#endif

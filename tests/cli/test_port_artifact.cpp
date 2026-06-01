// v1.27.0 (Phase 1.2): TDD coverage for ICMG-PORT v1 artifact serialize/parse.
//
// Closes v1.24.0 TDD gap. Covers magic header, FNV-128 hash verification,
// header K:V parsing, and round-trip fidelity.

#include "../test_main.hpp"
#include "../../src/cli/commands/port_artifact.hpp"

#include <string>

using icmg::cli::port_artifact::fnv128hex;
using icmg::cli::port_artifact::parseArtifact;
using icmg::cli::port_artifact::serializeArtifact;

// ---- fnv128hex --------------------------------------------------------------

TEST("port_artifact: fnv128hex empty string is deterministic") {
    auto h1 = fnv128hex("");
    auto h2 = fnv128hex("");
    ASSERT_EQ(h1, h2);
    ASSERT_EQ((int)h1.size(), 32);  // 16 bytes × 2 hex chars
}

TEST("port_artifact: fnv128hex different inputs → different hashes") {
    auto h1 = fnv128hex("alpha");
    auto h2 = fnv128hex("beta");
    ASSERT_TRUE(h1 != h2);
    ASSERT_EQ((int)h1.size(), 32);
    ASSERT_EQ((int)h2.size(), 32);
}

TEST("port_artifact: fnv128hex hex-only output") {
    auto h = fnv128hex("test payload 123");
    for (char c : h) {
        bool hex_ok = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
        ASSERT_TRUE(hex_ok);
    }
}

// ---- serializeArtifact ------------------------------------------------------

TEST("port_artifact: serialize produces ICMG-PORT v1 magic header") {
    std::string blob = serializeArtifact("test-bundle", 2, 100,
                                          R"({"files":[]})");
    ASSERT_TRUE(blob.find("ICMG-PORT v1") == 0);
}

TEST("port_artifact: serialize includes FILES/RAW/HASH header lines") {
    std::string payload = R"({"files":[{"path":"a","content":"x"}]})";
    std::string blob = serializeArtifact("b", 1, 42, payload);
    ASSERT_TRUE(blob.find("FILES: 1") != std::string::npos);
    ASSERT_TRUE(blob.find("RAW: 42") != std::string::npos);
    ASSERT_TRUE(blob.find("HASH: " + fnv128hex(payload)) != std::string::npos);
}

TEST("port_artifact: serialize separates header from payload with \\n---\\n") {
    std::string payload = "JSONHERE";
    std::string blob = serializeArtifact("b", 0, 0, payload);
    ASSERT_TRUE(blob.find("\n---\n" + payload) != std::string::npos);
}

// ---- parseArtifact valid path -----------------------------------------------

TEST("port_artifact: round-trip serialize → parse preserves payload") {
    std::string payload = R"({"files":[{"path":"src/main.cpp","content":"int main(){}"}]})";
    std::string blob = serializeArtifact("rt", 1, 12, payload);
    auto p = parseArtifact(blob);
    ASSERT_TRUE(p.ok);
    ASSERT_TRUE(p.error.empty());
    ASSERT_EQ(p.files, 1);
    ASSERT_EQ((long long)p.raw_bytes, 12LL);
    ASSERT_EQ(p.payload, payload);
}

TEST("port_artifact: parse populates hash field") {
    std::string payload = "P";
    std::string blob = serializeArtifact("x", 0, 0, payload);
    auto p = parseArtifact(blob);
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.hash, fnv128hex(payload));
}

// ---- parseArtifact rejection paths ------------------------------------------

TEST("port_artifact: parse rejects wrong magic") {
    std::string blob = "ICMG-PORT v2\nFILES: 0\n---\npayload";
    auto p = parseArtifact(blob);
    ASSERT_TRUE(!p.ok);
    ASSERT_TRUE(p.error.find("bad magic") != std::string::npos
             || p.error.find("wrong version") != std::string::npos);
}

TEST("port_artifact: parse rejects missing magic line") {
    std::string blob = "NOT_ICMG\nFILES: 0\n---\npayload";
    auto p = parseArtifact(blob);
    ASSERT_TRUE(!p.ok);
}

TEST("port_artifact: parse rejects missing payload separator") {
    std::string blob = "ICMG-PORT v1\nFILES: 0\nHASH: deadbeef\n";
    auto p = parseArtifact(blob);
    ASSERT_TRUE(!p.ok);
    ASSERT_TRUE(p.error.find("separator") != std::string::npos
             || p.error.find("payload") != std::string::npos);
}

TEST("port_artifact: parse rejects hash mismatch (corrupted payload)") {
    // Hand-craft blob where HASH does not match payload.
    std::string blob =
        "ICMG-PORT v1\n"
        "FILES: 0\n"
        "RAW: 0\n"
        "HASH: 00000000000000000000000000000000\n"
        "---\n"
        "actual payload here";
    auto p = parseArtifact(blob);
    ASSERT_TRUE(!p.ok);
    ASSERT_TRUE(p.error.find("hash mismatch") != std::string::npos
             || p.error.find("corrupted") != std::string::npos);
}

TEST("port_artifact: parse empty blob rejected") {
    auto p = parseArtifact("");
    ASSERT_TRUE(!p.ok);
}

TEST("port_artifact: parse truncated header rejected") {
    auto p = parseArtifact("ICMG-PORT v1");  // no newline
    ASSERT_TRUE(!p.ok);
}

// ---- Edge cases -------------------------------------------------------------

TEST("port_artifact: parse handles empty payload (consistent hash)") {
    std::string blob = serializeArtifact("empty", 0, 0, "");
    auto p = parseArtifact(blob);
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.payload, std::string(""));
}

TEST("port_artifact: parse handles payload containing --- delimiter chars") {
    std::string payload = "before---\nmiddle\n---after";
    std::string blob = serializeArtifact("tricky", 0, 0, payload);
    auto p = parseArtifact(blob);
    ASSERT_TRUE(p.ok);
    ASSERT_EQ(p.payload, payload);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif

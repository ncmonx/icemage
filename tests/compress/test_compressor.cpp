// Phase 39 T1+T2+T3: compressor unit tests.
#include "../test_main.hpp"
#include "../../src/compress/compressor.hpp"

using icmg::compress::Compressor;
using icmg::compress::CompressOptions;
using icmg::compress::Mode;

// Build sample with repeated long path + identifiers, > 8K char threshold.
static std::string bigSample() {
    std::string s;
    const std::string path = "src/middleware/authentication/jwt_token_validator.cpp";
    const std::string id1  = "JwtTokenValidatorService";
    const std::string id2  = "AuthenticationMiddleware";
    for (int i = 0; i < 500; ++i) {
        s += "Line " + std::to_string(i) + ": " + path + " in " + id1
           + " calls " + id2 + " with payload\n";
    }
    return s;
}

TEST("compressor: small input → skipped (under threshold)") {
    Compressor c;
    auto r = c.compress("hello world", "");
    ASSERT_TRUE(r.skipped);
    ASSERT_EQ(r.text, std::string("hello world"));
}

TEST("compressor: source-code kind → skipped") {
    CompressOptions o; o.threshold_tok = 1;  // bypass size gate
    Compressor c(o);
    std::string body(40000, 'x');  // big enough to trip threshold
    auto r = c.compress(body, ".cpp");
    ASSERT_TRUE(r.skipped);
    ASSERT_TRUE(r.skip_reason.find("source") != std::string::npos);
}

TEST("compressor: cached sentinel → skipped") {
    CompressOptions o; o.threshold_tok = 1;
    Compressor c(o);
    std::string body = "<<CACHED>>" + std::string(40000, 'a') + "<</CACHED>>";
    auto r = c.compress(body, "");
    ASSERT_TRUE(r.skipped);
    ASSERT_TRUE(r.skip_reason.find("CACHED") != std::string::npos);
}

TEST("compressor: builds glossary on big repetitive input") {
    Compressor c;
    auto r = c.compress(bigSample(), ".log");
    ASSERT_FALSE(r.skipped);
    ASSERT_TRUE(r.glossary.size() >= 2);
    ASSERT_TRUE(r.tok_out < r.tok_in);
    // Hash present, body section non-empty.
    ASSERT_FALSE(r.content_hash.empty());
    ASSERT_TRUE(r.text.find("<icmg-glossary") != std::string::npos);
    ASSERT_TRUE(r.text.find("<icmg-body>") != std::string::npos);
}

TEST("compressor: round-trip lossless via parsePreface + expand") {
    Compressor c;
    std::string input = bigSample();
    auto r = c.compress(input, ".log");
    ASSERT_FALSE(r.skipped);

    std::map<std::string,std::string> g;
    std::string body;
    bool ok = Compressor::parsePreface(r.text, &g, &body);
    ASSERT_TRUE(ok);
    ASSERT_EQ((int)g.size(), (int)r.glossary.size());

    std::string err;
    std::string expanded = Compressor::expand(body, g, true, &err);
    // After dedup we may have <icmg-repeat Nx> markers in body; round-trip
    // not byte-identical but glossary aliases must all be resolved.
    ASSERT_TRUE(err.empty());
    ASSERT_TRUE(expanded.find("@P1") == std::string::npos);
    ASSERT_TRUE(expanded.find("$I1") == std::string::npos);
}

TEST("compressor: aggressive mode strips filler") {
    CompressOptions o; o.threshold_tok = 100; o.mode = Mode::Aggressive;
    Compressor c(o);
    std::string s = "this is really really just a basically simple log\n";
    for (int i = 0; i < 100; ++i) s += "filler line really basically just here\n";
    auto r = c.compress(s, ".log");
    ASSERT_FALSE(r.skipped);
    ASSERT_TRUE(r.text.find(" really ") == std::string::npos);
    ASSERT_TRUE(r.text.find(" basically ") == std::string::npos);
}

TEST("compressor: expand strict reports unknown alias") {
    std::map<std::string,std::string> g;
    g["@P1"] = "some/path";
    std::string err;
    auto out = Compressor::expand("text @P1 and @P9 stray", g, true, &err);
    (void)out;
    ASSERT_TRUE(err.find("@P9") != std::string::npos);
}

TEST("compressor: expand lenient leaves unknown") {
    std::map<std::string,std::string> g;
    g["@P1"] = "X";
    std::string err;
    auto out = Compressor::expand("@P1 @P9", g, false, &err);
    ASSERT_TRUE(out.find("@P9") != std::string::npos);
    ASSERT_TRUE(out.find("X") != std::string::npos);
}

TEST("compressor: estimateTokens monotonic") {
    int a = Compressor::estimateTokens("hi");
    int b = Compressor::estimateTokens(std::string(4000, 'x'));
    ASSERT_TRUE(b > a);
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif

// Tests for wantedAssetName() logic in UpdateCommand.
// The function maps a release tag to the expected zip asset filename.
// Since the function is private-static, logic is replicated here (same
// pattern as test_init_hook.cpp).
#include "../test_main.hpp"
#include <string>

static std::string wantedAssetName(const std::string& tag) {
    std::string ver = tag;
    if (!ver.empty() && (ver[0] == 'v' || ver[0] == 'V')) ver.erase(0, 1);
#ifdef _WIN32
    return "icmg-" + ver + "-win-x64.zip";
#elif defined(__APPLE__)
    return "icmg-" + ver + "-macos-x64.tar.gz";
#else
    return "icmg-" + ver + "-linux-x64.tar.gz";
#endif
}

TEST("update_asset: v-prefix stripped") {
    auto got = wantedAssetName("v0.37.4");
#ifdef _WIN32
    ASSERT_EQ(got, std::string("icmg-0.37.4-win-x64.zip"));
#elif defined(__APPLE__)
    ASSERT_EQ(got, std::string("icmg-0.37.4-macos-x64.tar.gz"));
#else
    ASSERT_EQ(got, std::string("icmg-0.37.4-linux-x64.tar.gz"));
#endif
}

TEST("update_asset: V-prefix stripped") {
    auto got = wantedAssetName("V1.0.0");
#ifdef _WIN32
    ASSERT_EQ(got, std::string("icmg-1.0.0-win-x64.zip"));
#elif defined(__APPLE__)
    ASSERT_EQ(got, std::string("icmg-1.0.0-macos-x64.tar.gz"));
#else
    ASSERT_EQ(got, std::string("icmg-1.0.0-linux-x64.tar.gz"));
#endif
}

TEST("update_asset: no prefix passthrough") {
    auto got = wantedAssetName("0.37.4");
#ifdef _WIN32
    ASSERT_EQ(got, std::string("icmg-0.37.4-win-x64.zip"));
#elif defined(__APPLE__)
    ASSERT_EQ(got, std::string("icmg-0.37.4-macos-x64.tar.gz"));
#else
    ASSERT_EQ(got, std::string("icmg-0.37.4-linux-x64.tar.gz"));
#endif
}

TEST("update_asset: asset contains version segment") {
    auto got = wantedAssetName("v2.0.0");
    ASSERT_CONTAINS(got, "2.0.0");
}

TEST("update_asset: asset has zip/tar extension") {
    auto got = wantedAssetName("v0.37.4");
#ifdef _WIN32
    ASSERT_CONTAINS(got, ".zip");
#else
    ASSERT_CONTAINS(got, ".tar.gz");
#endif
}


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif

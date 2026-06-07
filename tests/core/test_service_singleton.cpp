// 2026-06-07: service singleton-guard naming (zombie-bloat fix).
// Global\ mutex needed elevation -> ACCESS_DENIED -> guard fell open -> 20+
// zombie services. Local\ works unprivileged so the guard holds.
#include "../test_main.hpp"
#include "../../src/core/service_singleton.hpp"

using namespace icmg::core;

TEST("service_singleton: Local namespace, per-user") {
    ASSERT_EQ(serviceMutexName("Administrator"),
              std::string("Local\\icmg-service-Administrator"));
    ASSERT_EQ(serviceMutexName("cahyo"),
              std::string("Local\\icmg-service-cahyo"));
}

TEST("service_singleton: never Global (elevation-required namespace)") {
    std::string n = serviceMutexName("anyuser");
    ASSERT_TRUE(n.rfind("Local\\", 0) == 0);
    ASSERT_TRUE(n.find("Global\\") == std::string::npos);
}

TEST("service_singleton: empty user falls back, still Local") {
    ASSERT_EQ(serviceMutexName(""), std::string("Local\\icmg-service-default"));
}

// ram-brain Phase C: daemon RCACHE_* shared cache via dispatch().
#include "../test_main.hpp"
#include "../../src/daemon/rule_daemon.hpp"
#include <nlohmann/json.hpp>

using icmg::daemon::RuleDaemon;

static std::string req(const std::string& tool, const std::string& stdin_) {
    nlohmann::json j; j["tool"] = tool; j["stdin"] = stdin_;
    return j.dump();
}

TEST("rcache-rpc: PUT then GET hit; FLUSH clears") {
    RuleDaemon d(":memory:");
    nlohmann::json inner; inner["key"] = "k"; inner["value"] = "v";
    d.dispatch(req("RCACHE_PUT", inner.dump()));
    auto got = d.dispatch(req("RCACHE_GET", "k"));
    ASSERT_TRUE(got.find("\"value\":\"v\"") != std::string::npos);
    ASSERT_TRUE(got.find("\"emit\":\"v\"") != std::string::npos);   // client reads emit
    d.dispatch(req("RCACHE_FLUSH", ""));
    auto miss = d.dispatch(req("RCACHE_GET", "k"));
    ASSERT_TRUE(miss.find("\"miss\":true") != std::string::npos);
}

TEST("rcache-rpc: STATS returns counters") {
    RuleDaemon d(":memory:");
    nlohmann::json inner; inner["key"] = "a"; inner["value"] = "1";
    d.dispatch(req("RCACHE_PUT", inner.dump()));
    d.dispatch(req("RCACHE_GET", "a"));   // hit
    auto st = d.dispatch(req("RCACHE_STATS", ""));
    ASSERT_TRUE(st.find("\"entries\":1") != std::string::npos);
    ASSERT_TRUE(st.find("\"hits\":1") != std::string::npos);
}

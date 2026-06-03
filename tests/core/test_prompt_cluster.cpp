// Greedy similar-prompt clustering for `icmg profile qa-frequent`.
#include "../test_main.hpp"
#include "../../src/core/prompt_cluster.hpp"
#include <string>
using namespace icmg::core;

TEST("cluster: near-duplicate prompts land in one cluster") {
    std::vector<std::string> prompts = {
        "how do I rebuild the msvc binary",
        "how do I rebuild the msvc binary now please",
        "what color is the sky",
    };
    auto cl = clusterSimilar(prompts, 0.4);
    ASSERT_EQ(cl.size(), size_t(2));            // the two builds group, sky is separate
    ASSERT_EQ(cl[0].members.size(), size_t(2)); // largest cluster first
}

TEST("cluster: all-distinct prompts yield one cluster each") {
    std::vector<std::string> prompts = {"alpha beta gamma", "delta epsilon zeta", "eta theta iota"};
    auto cl = clusterSimilar(prompts, 0.5);
    ASSERT_EQ(cl.size(), size_t(3));
}

TEST("cluster: identical prompts collapse into one cluster") {
    std::vector<std::string> prompts = {"run the build script", "run the build script", "run the build script"};
    auto cl = clusterSimilar(prompts, 0.5);
    ASSERT_EQ(cl.size(), size_t(1));
    ASSERT_EQ(cl[0].members.size(), size_t(3));
}

TEST("cluster: empty input yields no clusters") {
    std::vector<std::string> none;
    auto cl = clusterSimilar(none, 0.5);
    ASSERT_TRUE(cl.empty());
}

TEST("cluster: results are ordered largest-first") {
    std::vector<std::string> prompts = {
        "unique solo prompt here",
        "deploy the service to staging",
        "deploy the service to staging environment",
        "deploy the service to staging now",
    };
    auto cl = clusterSimilar(prompts, 0.4);
    ASSERT_TRUE(cl.size() >= 1);
    for (size_t i = 1; i < cl.size(); ++i)
        ASSERT_TRUE(cl[i - 1].members.size() >= cl[i].members.size());
}

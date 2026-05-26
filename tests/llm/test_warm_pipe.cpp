#include "llm/warm_pipe.hpp"
#include <cassert>
#include <iostream>

using namespace icmg::llm;

static void test_pipe_name_nonempty() {
    PipeConfig cfg;
    cfg.name = "icmg-test-warm";
    assert(!cfg.name.empty());
    assert(cfg.max_instances > 0);
}

int main() {
    test_pipe_name_nonempty();
    std::cout << "test_warm_pipe: 1/1 PASS\n";
    return 0;
}

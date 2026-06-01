#pragma once
#include "warm_pipe.hpp"
#include <string>

namespace icmg::llm {

struct WarmLoopConfig {
    PipeConfig pipe;
    int worker_threads = 4;
    std::string pid_file_path;
};

// Daemon main. Blocks until shutdown cmd or stop signal.
// Returns 0 on graceful exit, non-zero on init failure.
int runWarmLoop(const WarmLoopConfig& cfg);

}

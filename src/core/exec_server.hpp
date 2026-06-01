// v1.13.0: exec IPC server — handles CLI invocations in-process.
// Spawned as dedicated thread by ServiceLoop.

#pragma once

#include <atomic>

namespace icmg::core::exec_server {

// Run server (blocks). Returns when stop_flag goes true.
// Spawn workers per incoming request.
void run(std::atomic<bool>& stop_flag);

} // namespace icmg::core::exec_server

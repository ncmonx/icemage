// v1.13.0: log rotation — date-stamp rename + retention.
#pragma once

#include <string>

namespace icmg::core::log_rotate {

// Rotate `path` if size > max_bytes. Renames to `path.YYYY-MM-DD`.
// If rename target exists, appends `.HHMMSS`. Deletes rotated files
// older than retention_days. Idempotent + cheap (~1ms).
void rotate(const std::string& path, size_t max_bytes, int retention_days);

// Rotate well-known icmg log files. Called by ServiceLoop on each tick.
void rotateIcmgLogs();

} // namespace icmg::core::log_rotate

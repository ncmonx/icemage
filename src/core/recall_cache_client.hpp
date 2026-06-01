// ram-brain: best-effort daemon roundtrip for the shared RecallCache. Every
// call is non-throwing and fails fast/silent when the daemon is down, so the
// recall hot path always falls back to local compute. Wraps RuleDaemonClient.
#pragma once
#include <optional>
#include <string>

namespace icmg { namespace core {

// Returns the daemon-cached value for `key`, or nullopt on miss / daemon-down.
std::optional<std::string> rcacheDaemonGet(const std::string& key);
// Best-effort store into the daemon's shared cache (no-op if daemon down).
void rcacheDaemonPut(const std::string& key, const std::string& value);
// Best-effort flush of the daemon's shared cache.
void rcacheDaemonFlush();

}} // namespace

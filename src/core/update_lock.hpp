#pragma once
// v1.78.4: shared updating.lock helpers.
// Extracted from update_cmd.cpp static functions so dispatcher.cpp can check
// the lock at startup and refuse to run during a binary swap window.
#include <filesystem>
#include <ctime>

namespace icmg::core {

std::filesystem::path updatingLockPath();
void writeUpdatingLock();
void clearUpdatingLock();

// Returns true iff updating.lock exists AND was written < 300s ago.
// Used at startup to bail out before grabbing the exe file handle.
bool isUpdatingLockFresh();

} // namespace icmg::core

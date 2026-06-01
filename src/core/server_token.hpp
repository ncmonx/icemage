#pragma once
// v1.68 S2: per-user auth token for the icmg-server named-pipe daemon.
// The token file lives in the user's global icmg dir; any pipe client must
// present the matching token or the server refuses to dispatch (incl. shutdown).
#include <string>

namespace icmg::core {

// Path to the server token file (icmgGlobalDir()/server.token).
std::string serverTokenPath();

// Read the existing token, or generate+persist a new 32-hex token if absent.
// On POSIX the file is chmod 0600. Returns "" only on unrecoverable I/O error.
std::string loadOrCreateServerToken();

// Read the existing token without creating one. Returns "" if no token file.
std::string readServerToken();

// Constant-scan equality. Fails closed: if 'expected' is empty, always false.
bool tokenMatches(const std::string& expected, const std::string& got);

} // namespace icmg::core

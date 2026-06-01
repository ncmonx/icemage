#pragma once
// v1.68 S3: filesystem secret scanning. Walks a directory tree, runs the
// existing scanSecrets() patterns over each text file, and reports findings
// with file path + 1-based line number. Backs the `icmg scan` command.
#include <cstddef>
#include <string>
#include <vector>

namespace icmg::core {

struct FileSecretFinding {
    std::string path;     // path to the file containing the secret
    size_t      line;     // 1-based line number of the match
    std::string type;     // detector type (e.g. "AWS_ACCESS_KEY")
    std::string preview;  // redacted (default) or raw matched text
};

struct ScanOpts {
    bool   redact_preview = true;               // <REDACTED:TYPE> vs raw match
    size_t max_file_bytes = 2 * 1024 * 1024;    // skip files larger than this
};

// Recursively scan 'root'. Skips VCS/build/vendor dirs and binary files.
// Returns one finding per secret match, in directory-walk order.
std::vector<FileSecretFinding> scanTree(const std::string& root,
                                        const ScanOpts& opts = ScanOpts{});

} // namespace icmg::core

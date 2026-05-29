// v1.68 S3: filesystem secret scanning — implementation.

#include "scan_logic.hpp"
#include "secret_scanner.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace icmg::core {

namespace {

// Directories never worth scanning (VCS metadata, build output, vendored deps).
bool isSkippedDir(const std::string& name) {
    static const char* kSkip[] = {
        ".git", ".svn", ".hg", ".icmg", "build", "node_modules",
        "third_party", "vendor", "dist", "__pycache__", ".vs", ".idea"
    };
    for (const char* s : kSkip) if (name == s) return true;
    return false;
}

// Heuristic binary sniff: a NUL byte in the first 8 KB => treat as binary.
bool looksBinary(const std::string& content) {
    size_t n = std::min<size_t>(content.size(), 8192);
    for (size_t i = 0; i < n; ++i) if (content[i] == '\0') return true;
    return false;
}

// 1-based line number for a byte offset (count newlines up to offset).
size_t lineOf(const std::string& text, size_t offset) {
    size_t line = 1;
    size_t lim = std::min(offset, text.size());
    for (size_t i = 0; i < lim; ++i) if (text[i] == '\n') ++line;
    return line;
}

std::string readFile(const fs::path& p, size_t cap, bool& too_big) {
    too_big = false;
    std::error_code ec;
    auto sz = fs::file_size(p, ec);
    if (!ec && sz > cap) { too_big = true; return ""; }
    std::ifstream f(p, std::ios::binary);
    if (!f) return "";
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

} // namespace

std::vector<FileSecretFinding> scanTree(const std::string& root,
                                        const ScanOpts& opts) {
    std::vector<FileSecretFinding> out;
    std::error_code ec;

    fs::recursive_directory_iterator it(root,
        fs::directory_options::skip_permission_denied, ec);
    if (ec) return out;
    fs::recursive_directory_iterator end;

    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const fs::path& p = it->path();

        if (it->is_directory(ec)) {
            if (isSkippedDir(p.filename().string())) {
                it.disable_recursion_pending();   // do not descend
            }
            continue;
        }
        if (!it->is_regular_file(ec)) continue;

        bool too_big = false;
        std::string content = readFile(p, opts.max_file_bytes, too_big);
        if (too_big || content.empty()) continue;
        if (looksBinary(content)) continue;

        auto matches = scanSecrets(content);
        for (const auto& m : matches) {
            FileSecretFinding f;
            f.path = p.string();
            f.line = lineOf(content, m.offset);
            f.type = m.type;
            if (opts.redact_preview) {
                f.preview = "<REDACTED:" + m.type + ">";
            } else {
                f.preview = m.match;
            }
            out.push_back(std::move(f));
        }
    }
    return out;
}

} // namespace icmg::core

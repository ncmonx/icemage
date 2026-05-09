#include "url_sanitize.hpp"
#include <regex>
#include <sstream>

namespace icmg::core {

namespace {

// Shell metacharacters that, if injected via URL into a shell command,
// can hijack execution. Even though we always quote the URL with double
// quotes when invoking curl, a `"` inside the URL can break out, and `$`,
// backtick, `\` can interpolate inside quotes on POSIX shells.
const char* kForbiddenChars = "\"`$\\;|&\n\r\t<>";

bool isControlChar(unsigned char c) {
    return c < 0x20 || c == 0x7f;
}

} // namespace

bool isUrlSafe(const std::string& url) {
    std::string reason;
    return validateUrl(url, reason);
}

bool validateUrl(const std::string& url, std::string& reason) {
    if (url.empty()) {
        reason = "empty URL";
        return false;
    }
    if (url.size() > 4096) {
        reason = "URL too long (>4096 chars)";
        return false;
    }
    // Scheme: http/https only. Reject file://, gopher://, etc.
    if (url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0) {
        reason = "URL must use http:// or https:// scheme";
        return false;
    }
    for (unsigned char c : url) {
        if (isControlChar(c)) {
            reason = "URL contains control character";
            return false;
        }
        for (const char* f = kForbiddenChars; *f; ++f) {
            if (c == (unsigned char)*f) {
                reason = std::string("URL contains forbidden character '") + *f + "'";
                return false;
            }
        }
    }
    // Optional: validate basic shape via regex (host present).
    static const std::regex shape(R"(^https?://[A-Za-z0-9._~:\-]+(/[^\s]*)?$)");
    if (!std::regex_match(url, shape)) {
        reason = "URL shape rejected (host/path validation)";
        return false;
    }
    return true;
}

} // namespace icmg::core

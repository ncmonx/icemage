#include "atomic_file.hpp"

#include <fstream>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#define ICMG_GETPID _getpid
#else
#include <unistd.h>
#define ICMG_GETPID getpid
#endif

namespace fs = std::filesystem;

namespace icmg::core {

bool isValidUtf8(std::string_view s) {
    size_t i = 0;
    const size_t n = s.size();
    while (i < n) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { ++i; continue; }
        int len;
        unsigned cp;
        if ((c & 0xE0) == 0xC0)      { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else return false;  // continuation byte or 0xF8+ lead
        if (i + len > n) return false;
        for (int k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        static constexpr unsigned kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < kMin[len]) return false;                 // overlong
        if (cp > 0x10FFFF) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;   // surrogate
        i += len;
    }
    return true;
}

bool writeFileAtomic(const fs::path& target, const std::string& content, std::string* err) {
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    const fs::path dir = target.parent_path();
    std::error_code ec;
    if (!dir.empty() && !fs::is_directory(dir, ec))
        return fail("directory does not exist: " + dir.string());

    fs::path tmp = target;
    tmp += ".tmp-" + std::to_string(ICMG_GETPID());
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return fail("cannot open temp file: " + tmp.string());
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        f.flush();
        if (!f) {
            f.close();
            fs::remove(tmp, ec);
            return fail("write failed: " + tmp.string());
        }
    }
    fs::rename(tmp, target, ec);  // replaces an existing target (MoveFileEx on Windows)
    if (ec) {
        std::error_code ec2;
        fs::remove(tmp, ec2);
        return fail("rename failed: " + ec.message());
    }
    return true;
}

}  // namespace icmg::core

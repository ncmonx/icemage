#include "win_utf8.hpp"

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <shellapi.h>
#  include <cwctype>
#endif

namespace icmg::core {

namespace {
constexpr char32_t kReplacement = 0xFFFD;

void appendCodePoint(std::wstring& out, char32_t cp) {
    if constexpr (sizeof(wchar_t) == 2) {
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
            return;
        }
    }
    out += static_cast<wchar_t>(cp);
}

void appendUtf8(std::string& out, char32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}
}  // namespace

std::wstring utf8ToWide(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        const auto b0 = static_cast<unsigned char>(s[i]);
        int len = b0 < 0x80 ? 1 : (b0 >> 5) == 0x6 ? 2 : (b0 >> 4) == 0xE ? 3 : (b0 >> 3) == 0x1E ? 4 : 0;
        if (len == 0 || i + len > s.size()) { appendCodePoint(out, kReplacement); ++i; continue; }
        char32_t cp = len == 1 ? b0 : len == 2 ? (b0 & 0x1F) : len == 3 ? (b0 & 0x0F) : (b0 & 0x07);
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            const auto b = static_cast<unsigned char>(s[i + k]);
            if ((b & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (b & 0x3F);
        }
        // Reject overlong forms, surrogates and out-of-range values.
        static const char32_t kMin[] = {0, 0, 0x80, 0x800, 0x10000};
        if (!ok || cp < kMin[len] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            appendCodePoint(out, kReplacement); ++i; continue;
        }
        appendCodePoint(out, cp);
        i += len;
    }
    return out;
}

std::string wideToUtf8(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        char32_t cp = static_cast<char32_t>(w[i]);
        if constexpr (sizeof(wchar_t) == 2) {
            if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size()) {
                const char32_t lo = static_cast<char32_t>(w[i + 1]);
                if (lo >= 0xDC00 && lo <= 0xDFFF) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    ++i;
                }
            }
        }
        if ((cp >= 0xD800 && cp <= 0xDFFF) || cp > 0x10FFFF) cp = kReplacement;
        appendUtf8(out, cp);
    }
    return out;
}

#ifdef _WIN32
std::vector<std::string> commandLineToUtf8Argv(const wchar_t* cmdline) {
    std::vector<std::string> out;
    if (!cmdline) return out;
    int n = 0;
    LPWSTR* wargv = CommandLineToArgvW(cmdline, &n);
    if (!wargv) return out;
    out.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) out.push_back(wideToUtf8(wargv[i]));
    LocalFree(wargv);
    return out;
}

std::vector<std::string> processArgvUtf8() {
    return commandLineToUtf8Argv(GetCommandLineW());
}

std::wstring wideEnvBlockWith(const std::string& name, const std::string& value) {
    const std::wstring wname = utf8ToWide(name);
    auto sameName = [&](const std::wstring& entry, size_t eq) {
        if (eq != wname.size()) return false;
        for (size_t k = 0; k < eq; ++k)
            if (std::towupper(entry[k]) != std::towupper(wname[k])) return false;
        return true;
    };
    std::wstring block;
    if (LPWCH envs = GetEnvironmentStringsW()) {
        for (LPWCH p = envs; *p; p += wcslen(p) + 1) {
            std::wstring entry(p);
            const size_t eq = entry.find(L'=');
            // eq == 0 is a hidden "=C:=C:\..." drive entry: keep it.
            if (eq != std::wstring::npos && eq > 0 && sameName(entry, eq)) continue;
            block += entry;
            block += L'\0';
        }
        FreeEnvironmentStringsW(envs);
    }
    block += wname + L"=" + utf8ToWide(value);
    block += L'\0';
    block += L'\0';  // double-NUL terminator
    return block;
}
#endif

}  // namespace icmg::core

#pragma once
// core::pathU8 -- fs::path -> UTF-8 std::string that never throws.
//
// On Windows, fs::path::string()/generic_string() convert through the ANSI
// code page and raise system error 1113 ("No mapping for the Unicode
// character exists in the target multi-byte code page") for names the ACP
// cannot represent -- e.g. the Cygwin-mangled "C\uF03AUsers..." files or any
// CJK name on a Latin code page. u8string() always succeeds.
//
// Shared home for the helper that previously lived as private copies in
// find_cmd.cpp and maintain_cmd.cpp (regression 2026-10-16: files_cmd still
// used raw .string() and crashed on such names).
#include <filesystem>
#include <string>

namespace icmg::core {

inline std::string pathU8(const std::filesystem::path& p) {
    auto u8 = p.u8string();  // std::string in C++17, std::u8string in C++20
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// generic_string() twin: forward slashes, same no-throw guarantee.
inline std::string pathU8Generic(const std::filesystem::path& p) {
    auto u8 = p.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

}  // namespace icmg::core

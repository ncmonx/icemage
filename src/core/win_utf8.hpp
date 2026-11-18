#pragma once
// core::win_utf8 -- keep text UTF-8 inside icmg, wide at the Win32 boundary.
//
// #285: icmg was ANSI end to end on Windows. `main(int, char**)` received
// argv already converted to the ANSI code page, and children were spawned
// with CreateProcessA + an ANSI environment block, so any character outside
// cp1252 (e.g. U+2502) was best-fitted to '|' and Latin-1 text was
// double-encoded. The rule now: every std::string in icmg is UTF-8; convert
// to UTF-16 only when calling a W API (CreateProcessW, GetCommandLineW,
// GetEnvironmentStringsW).
//
// utf8ToWide / wideToUtf8 are portable (wchar_t is UTF-16 on Windows and
// UTF-32 elsewhere) so they are testable everywhere. Invalid UTF-8 bytes map
// to U+FFFD rather than throwing.
#include <string>
#include <vector>

namespace icmg::core {

std::wstring utf8ToWide(const std::string& s);
std::string  wideToUtf8(const std::wstring& w);

#ifdef _WIN32
// Split a wide command line (CommandLineToArgvW rules) into UTF-8 args.
std::vector<std::string> commandLineToUtf8Argv(const wchar_t* cmdline);

// This process's argv as UTF-8, read from GetCommandLineW() -- independent of
// the ANSI code page and of the application manifest.
std::vector<std::string> processArgvUtf8();

// Wide environment block = current environment (GetEnvironmentStringsW) with
// NAME=value set/replaced, double-NUL terminated. Pass .data() as
// lpEnvironment together with CREATE_UNICODE_ENVIRONMENT.
std::wstring wideEnvBlockWith(const std::string& name, const std::string& value);
#endif

}  // namespace icmg::core

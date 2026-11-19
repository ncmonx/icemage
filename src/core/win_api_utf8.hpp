#pragma once
// core::win_api_utf8 -- UTF-8 drop-ins for the ANSI Win32 calls icmg used.
//
// #285 follow-up: every std::string in icmg is UTF-8 (see win_utf8.hpp), but
// many call sites still spawned children with CreateProcessA, read their own
// path with GetModuleFileNameA and opened files with ShellExecuteA. Those
// convert through the ANSI code page, so a non-ASCII install dir, project path
// or argument became '?' (or was double-encoded). The wrappers below take the
// same UTF-8 strings and call the W API, so call sites only change the name.
// Windows-only; a no-op elsewhere. Include it AFTER the file's own
// <windows.h> so that file's WIN32_LEAN_AND_MEAN / winsock order governs.
#ifdef _WIN32
#  include <windows.h>
#  include <string>

namespace icmg::core {

// CreateProcessA signature, but app / cmdline / cwd are UTF-8 and the call is
// CreateProcessW. The STARTUPINFOA fields (flags, show window, std handles)
// are carried over as-is. `env` is passed through untouched: an ANSI block
// unless `flags` has CREATE_UNICODE_ENVIRONMENT. `cmdline` may be null.
BOOL createProcessUtf8(const char* app, char* cmdline,
                       LPSECURITY_ATTRIBUTES procAttrs,
                       LPSECURITY_ATTRIBUTES threadAttrs, BOOL inheritHandles,
                       DWORD flags, LPVOID env, const char* cwd,
                       const STARTUPINFOA* si, PROCESS_INFORMATION* pi);

// GetModuleFileNameW as UTF-8 (null module = this exe). Empty on failure.
// No MAX_PATH cap: the buffer grows until the full path fits.
std::string moduleFileNameUtf8(HMODULE module = nullptr);

// ShellExecuteW(nullptr, verb, target, params, nullptr, show). Returns true on
// success (the API's > 32 rule). `params` may be null.
bool shellExecuteUtf8(const char* verb, const std::string& target,
                      const char* params = nullptr, int show = SW_SHOWNORMAL);

}  // namespace icmg::core
#endif

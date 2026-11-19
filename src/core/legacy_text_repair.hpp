#pragma once
// core::legacy_text_repair -- one-time rewrite of pre-UTF-8 text in an icmg DB.
//
// #285 follow-up: icmg.exe now runs with a UTF-8 ANSI code page (manifest
// activeCodePage), so std::string <-> fs::path decode UTF-8. Builds before
// that wrote paths/text through the SYSTEM code page (cp1252 "caf\xE9"), and
// fs::path(row) on such a row throws system_error 1113. repairLegacyAnsiText
// walks every TEXT value of every ordinary table, re-encodes the ones that are
// not valid UTF-8 from `codePage` to UTF-8, and records a marker so the scan
// happens once per DB. BLOB values, virtual tables and their shadow tables are
// never touched (external-content FTS follows through its UPDATE triggers).
#include <string>
#include <string_view>

namespace icmg::core {

class Db;

// Strict UTF-8 check lives in atomic_file.hpp (core::isValidUtf8).
bool isValidUtf8(std::string_view s);

// Decode `s` from Windows code page `codePage` and re-encode as UTF-8.
// Returns `s` unchanged off Windows or when the conversion fails.
std::string legacyAnsiToUtf8(const std::string& s, unsigned codePage);

// The code page old builds used: the system default ANSI code page
// (independent of this process's manifest). 0 off Windows.
unsigned systemAnsiCodePage();

// One-time repair (marker table `icmg_text_repair`). Returns rows rewritten.
// A UTF-8 / zero code page means nothing legacy can exist: no-op, no marker.
// Never throws: a failed pass is rolled back and retried next start.
int repairLegacyAnsiText(Db& db, unsigned codePage);

}  // namespace icmg::core

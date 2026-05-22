// v1.25.0 (W3): compressed-write expander.
//
// Detects `@@ICMG-DIFF` / `@@ICMG-TPL` / `@@ICMG-RAW` / `@@ICMG-GLOSS` magic
// headers in AI-emitted Write content. Returns expanded content. On parse
// failure or unknown header, returns input verbatim (pass-through) so disk
// content never gets corrupted by a malformed header.
//
// Telemetry: each expand records into `write_compressions` table.

#pragma once

#include <string>

namespace icmg::compress {

enum class WriteMode { Raw, Diff, Template, Glossary, Auto };

struct ExpandResult {
    bool ok = true;             // false = parse failure, content passed through
    std::string mode;           // diff|template|glossary|raw — the detected mode
    std::string content;        // expanded bytes ready for disk
    std::string error;          // non-empty when ok=false
    int bytes_in = 0;
    int bytes_out = 0;
};

// Expand AI-emitted content. `base_path` is the target file (used to read
// existing content for diff base + verify SHA). Empty when new file.
ExpandResult expandCompressedWrite(const std::string& ai_content,
                                    const std::string& base_path);

}  // namespace icmg::compress

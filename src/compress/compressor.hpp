// Phase 39 T1: semantic prompt compressor.
// Lossless mode: dedup + reversible glossary substitution (paths, identifiers).
// Aggressive mode (T1+): also strips boilerplate filler words.
//
// Crucially NOT byte compression — Claude tokenizes plaintext, so binary blobs
// become garbage. This is a token-reduction layer with a reversible mapping
// preface so downstream tools (or `icmg expand`) can restore the original text.
//
// Output format:
//   <icmg-glossary v=1 hash=ABC>
//   @P1=very/long/path
//   $I1=LongIdentifier
//   </icmg-glossary>
//   <icmg-body>
//   ...text with @P1, $I1 aliases...
//   </icmg-body>

#pragma once
#include <string>
#include <vector>
#include <map>
#include <cstdint>

namespace icmg::compress {

enum class Mode {
    Lossless,    // dedup + glossary only — round-trip exact
    Aggressive   // + filler-strip — slightly lossy but still readable
};

struct CompressOptions {
    Mode mode = Mode::Lossless;
    int  min_path_len = 20;       // path candidate ≥ N chars
    int  min_path_freq = 3;       // ≥ N occurrences before glossary entry
    int  min_ident_len = 8;       // identifier ≥ N chars
    int  min_ident_freq = 5;
    int  threshold_tok = 8000;    // skip compress when est-tokens < this
    bool respect_cache_sentinel = true;  // pass-through <<CACHED>>...<</CACHED>>
};

struct CompressResult {
    std::string text;             // compressed output (with glossary preface)
    std::string body_only;        // body without glossary block
    std::map<std::string, std::string> glossary;   // alias → original
    int  tok_in  = 0;             // estimated input tokens
    int  tok_out = 0;             // estimated output tokens
    int  bytes_in  = 0;
    int  bytes_out = 0;
    int  elapsed_ms = 0;
    bool skipped = false;         // true → returned input unchanged
    std::string skip_reason;      // populated when skipped=true
    std::string content_hash;     // FNV1a hex of input
    Mode mode_used = Mode::Lossless;
};

class Compressor {
public:
    explicit Compressor(const CompressOptions& opts = {}) : opts_(opts) {}

    // Tokenize estimate (BPE ≈ 0.75 tok / word for ASCII). Cheap heuristic;
    // not byte-exact to Anthropic but ±10% acceptable for gating.
    static int estimateTokens(const std::string& s);

    // FNV1a-64 hex.
    static std::string contentHash(const std::string& s);

    // Decide whether to compress given content + signals.
    bool shouldCompress(const std::string& input,
                        const std::string& content_kind,
                        std::string* skip_reason) const;

    // Compress. Always succeeds (returns skipped result on early-exit).
    CompressResult compress(const std::string& input,
                            const std::string& content_kind = "");

    // Expand: replace aliases per glossary back into originals.
    // Strict=true: error on unknown alias. Strict=false: leave as-is.
    static std::string expand(const std::string& text,
                              const std::map<std::string,std::string>& glossary,
                              bool strict = true,
                              std::string* err = nullptr);

    // Parse glossary preface from compressed text → (glossary, body).
    // Returns false if no preface block found.
    static bool parsePreface(const std::string& compressed,
                             std::map<std::string,std::string>* glossary,
                             std::string* body);

private:
    CompressOptions opts_;
};

} // namespace icmg::compress

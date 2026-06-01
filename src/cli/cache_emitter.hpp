// Phase 40 T1: Anthropic prompt-cache sentinel emitter.
//
// Wraps a stable prefix region in markers that:
//   1. Tell downstream Anthropic API client to attach `cache_control`
//      ({type: "ephemeral", ttl_seconds: N}) to that block.
//   2. Are recognized by `icmg compress` as pass-through (sentinel
//      `<<CACHED>>` matches Phase 39 skip rule), preserving cache integrity.
//
// Output format:
//   <<CACHED ttl=3600>>
//   <stable content>
//   <</CACHED>>
//
// Why a string sentinel rather than direct API JSON?
//   icmg outputs to stdout/file; multiple downstream consumers (claude CLI,
//   bash pipes, file editors). Sentinels are tool-agnostic — any client that
//   wraps them with `cache_control` benefits; clients that don't, see plain
//   text. No coupling to Anthropic SDK version.
#pragma once
#include <string>

namespace icmg::cli {

struct CacheEmitOptions {
    int ttl_seconds = 3600;   // 1h default; Anthropic min 5m, max 1h ephemeral
};

// Wrap text in cache sentinels. Idempotent — re-wrap detected and skipped.
std::string wrapCachePrefix(const std::string& text,
                             const CacheEmitOptions& opts = {});

// Detect already-wrapped text (avoid double-wrap).
bool hasCacheWrap(const std::string& text);

} // namespace icmg::cli

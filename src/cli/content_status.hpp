#pragma once
// content_status.hpp — pure classification for `icmg context` file-content read.
//
// Bug fixed (2026-06-14, P3): the content-read block in bundle_cmd.cpp gated
// success on `!body.empty()`, so a file that OPENED FINE but was 0 bytes (e.g.
// now.md right after NDC rotation, an empty remember.md, any freshly-created
// file) fell through to a misleading "Content unavailable (graph path
// mismatch)" message — sending the agent chasing a path problem that did not
// exist. The real distinction is whether ANY candidate path opened at all
// (tracked by a non-empty `resolved`), independent of whether the file has
// content. This pure helper makes that 3-way decision unit-testable.
#include <string>

namespace icmg::cli {

enum class ContentStatus {
    Ok,           // a candidate opened AND the file has content
    Empty,        // a candidate opened but the file is 0 bytes (valid, just empty)
    Unavailable,  // no candidate path could be opened (a real path mismatch)
};

// `resolved` is the path that successfully opened (empty if none did).
// `body` is the bytes read from it.
inline ContentStatus classifyContent(const std::string& resolved,
                                     const std::string& body) {
    if (resolved.empty()) return ContentStatus::Unavailable;
    if (body.empty())     return ContentStatus::Empty;
    return ContentStatus::Ok;
}

} // namespace icmg::cli

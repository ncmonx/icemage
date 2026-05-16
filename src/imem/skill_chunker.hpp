#pragma once

#include <string>
#include <vector>

namespace icmg::imem {

/// A single chunk produced by splitting a skill markdown file.
struct SkillChunk {
    std::string heading;     ///< The heading text (or "(intro)" for pre-H2 content)
    std::string content;     ///< The raw markdown content of this chunk (excluding the heading line itself)
    std::string parent_path; ///< "<skill_node_key>/<slug>" — stable identifier
};

/// Splits a skill markdown document at H2 (and optionally H3) boundaries.
///
/// Rules:
///  - Each `## Heading` starts a new chunk.
///  - Content before the first H2 forms an "(intro)" chunk (dropped if empty).
///  - If an H2 section body exceeds 4 096 bytes, its `### Sub` headings are
///    each promoted to their own chunk; the H2 intro text (before the first H3)
///    forms a separate chunk only if non-empty.
///  - Empty chunks are dropped.
///  - Hard cap: at most 500 chunks returned; excess triggers a stderr warning.
class SkillChunker {
public:
    /// Split @p md at H2/H3 boundaries.
    /// @param md            Raw markdown string.
    /// @param skill_node_key Prefix used to build `parent_path` for every chunk.
    /// @return Vector of SkillChunk, at most 500 entries.
    static std::vector<SkillChunk> split(const std::string& md,
                                         const std::string& skill_node_key);

    /// Convert a heading string to a URL-safe slug:
    ///   lowercase, alnum + '-', no leading/trailing dashes, max 64 chars.
    static std::string slugify(const std::string& heading);
};

} // namespace icmg::imem

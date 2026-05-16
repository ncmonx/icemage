#include "skill_chunker.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <sstream>

namespace icmg::imem {

namespace {

constexpr std::size_t k_max_chunks     = 500;
constexpr std::size_t k_h2_split_size  = 4096; // bytes; H2 bodies larger than this get H3-promoted

// ---- helpers ---------------------------------------------------------------

bool starts_with(const std::string& s, const char* prefix) {
    std::size_t n = 0;
    while (prefix[n]) ++n;
    return s.size() >= n && s.compare(0, n, prefix, n) == 0;
}

/// Strip leading "## " / "### " prefix and return the rest (the heading text).
std::string strip_heading_prefix(const std::string& line, int level) {
    // level 2 → strip "## ", level 3 → strip "### "
    std::size_t prefix_len = static_cast<std::size_t>(level) + 1; // "##" + space
    if (line.size() <= prefix_len) return "";
    return line.substr(prefix_len);
}

/// Trim trailing whitespace/newlines from a string.
std::string rtrim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ' || s.back() == '\t'))
        s.pop_back();
    return s;
}

/// Build a SkillChunk (drops it if content is empty after rtrim).
/// Returns false if the chunk would be empty.
bool make_chunk(const std::string& heading,
                const std::string& content,
                const std::string& skill_node_key,
                std::vector<SkillChunk>& out)
{
    std::string c = rtrim(content);
    if (heading != "(intro)" && c.empty()) return false;
    if (heading == "(intro)" && c.empty()) return false;

    SkillChunk ch;
    ch.heading     = heading;
    ch.content     = std::move(c);
    ch.parent_path = skill_node_key + "/" + SkillChunker::slugify(heading);
    out.push_back(std::move(ch));
    return true;
}

/// Split an H2 body that is > k_h2_split_size at its H3 boundaries.
/// The H2 intro (text before first H3) is included as a chunk only if non-empty.
void split_h3(const std::string& h2_heading,
              const std::string& h2_body,
              const std::string& skill_node_key,
              std::vector<SkillChunk>& out)
{
    std::istringstream ss(h2_body);
    std::string line;

    std::string cur_heading; // empty → still in H2 intro
    std::string cur_content;

    auto flush = [&]() {
        if (cur_heading.empty()) {
            // H2 intro before first H3
            make_chunk(h2_heading, cur_content, skill_node_key, out);
        } else {
            make_chunk(cur_heading, cur_content, skill_node_key, out);
        }
        cur_content.clear();
    };

    bool has_h3 = false;
    while (std::getline(ss, line)) {
        if (starts_with(line, "### ")) {
            has_h3 = true;
            flush();
            cur_heading = strip_heading_prefix(line, 3);
        } else {
            cur_content += line + "\n";
        }
    }
    // flush last segment
    if (has_h3) {
        // flush the last H3 (or H2 intro if no H3 was ever found)
        if (!cur_heading.empty()) {
            make_chunk(cur_heading, cur_content, skill_node_key, out);
        } else {
            make_chunk(h2_heading, cur_content, skill_node_key, out);
        }
    } else {
        // No H3 found at all — emit as single H2 chunk (shouldn't normally happen
        // since we only call split_h3 when body > threshold, but be safe).
        make_chunk(h2_heading, h2_body, skill_node_key, out);
    }
}

} // anonymous namespace

// ---- public API ------------------------------------------------------------

std::string SkillChunker::slugify(const std::string& heading) {
    std::string result;
    result.reserve(heading.size());

    for (unsigned char ch : heading) {
        if (std::isalnum(ch)) {
            result += static_cast<char>(std::tolower(ch));
        } else if (!result.empty() && result.back() != '-') {
            result += '-';
        }
    }

    // Strip trailing dashes
    while (!result.empty() && result.back() == '-')
        result.pop_back();

    // Truncate to 64 chars (without splitting mid-word at a '-' boundary ideally,
    // but the spec just says max 64 chars so hard truncate is fine).
    if (result.size() > 64)
        result.resize(64);

    // Strip trailing dashes again after truncation
    while (!result.empty() && result.back() == '-')
        result.pop_back();

    return result;
}

std::vector<SkillChunk> SkillChunker::split(const std::string& md,
                                              const std::string& skill_node_key)
{
    if (md.empty()) return {};

    std::vector<SkillChunk> result;

    std::istringstream ss(md);
    std::string line;

    // Current H2 state
    std::string cur_h2_heading; // empty → in intro
    std::string cur_h2_body;

    auto flush_h2 = [&]() {
        const std::string& heading = cur_h2_heading.empty() ? "(intro)" : cur_h2_heading;

        if (cur_h2_heading.empty()) {
            // Intro block
            make_chunk("(intro)", cur_h2_body, skill_node_key, result);
        } else if (cur_h2_body.size() > k_h2_split_size) {
            // Large section → promote H3
            split_h3(cur_h2_heading, cur_h2_body, skill_node_key, result);
        } else {
            make_chunk(cur_h2_heading, cur_h2_body, skill_node_key, result);
        }
        (void)heading;
        cur_h2_body.clear();
    };

    while (std::getline(ss, line)) {
        if (starts_with(line, "## ")) {
            flush_h2();
            cur_h2_heading = strip_heading_prefix(line, 2);
        } else {
            cur_h2_body += line + "\n";
        }
    }
    // Flush the last H2 (or intro-only doc)
    flush_h2();

    // Apply hard cap
    if (result.size() > k_max_chunks) {
        std::cerr << "[SkillChunker] WARNING: chunk count " << result.size()
                  << " exceeds maximum " << k_max_chunks
                  << " for skill \"" << skill_node_key << "\"; truncating.\n";
        result.resize(k_max_chunks);
    }

    return result;
}

} // namespace icmg::imem

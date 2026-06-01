#pragma once
// v2.0.0 Phase 3: multimodal graph nodes. Turns ingested media (image/PDF OCR)
// into a first-class graph node (kind="multimodal") so `icmg context`, graph
// queries, and zones surface it like source files. Pure builder — unit-testable
// with no DB/filesystem; the command layer upserts the returned node.
#include "graph_node.hpp"

#include <cctype>
#include <string>

namespace icmg::graph {

// Collapse all whitespace runs (incl. newlines/tabs) to single spaces, trim
// ends, and truncate to maxLen with a trailing "..." marker when cut. Produces
// a one-line preview safe to store in GraphNode::context.
inline std::string mediaPreviewLine(const std::string& text, size_t maxLen = 200) {
    std::string out;
    out.reserve(text.size());
    bool in_space = false;
    for (char c : text) {
        unsigned char uc = static_cast<unsigned char>(c);
        if (std::isspace(uc)) {
            if (!out.empty()) in_space = true;  // defer — skip leading + collapse
            continue;
        }
        if (in_space) { out.push_back(' '); in_space = false; }
        out.push_back(c);
    }
    if (out.size() > maxLen) {
        out.resize(maxLen);
        out += "...";
    }
    return out;
}

// Build a multimodal graph node from an ingest result.
//   path      — media file path (becomes the node identity)
//   mediaType — "image" | "pdf" | "video" | ... (stored as lang)
//   extracted — OCR / extracted text (stored as a one-line context preview)
inline GraphNode buildMediaNode(const std::string& path,
                                const std::string& mediaType,
                                const std::string& extracted) {
    GraphNode n;
    n.path        = path;
    n.kind        = "multimodal";
    n.lang        = mediaType;
    n.context     = mediaPreviewLine(extracted);
    n.size_bytes  = static_cast<int64_t>(extracted.size());
    return n;
}

}  // namespace icmg::graph

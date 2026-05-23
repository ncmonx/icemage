// v1.27.0 (Phase 2): Template slot-fill engine impl.

#include "template_engine.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace icmg::compress {

using json = nlohmann::json;

std::string applyTemplate(const std::string& layout_tree,
                           const std::string& slots_json,
                           std::string* out_error) {
    json slots;
    try {
        slots = json::parse(slots_json);
    } catch (const std::exception& e) {
        if (out_error) *out_error = std::string("slots JSON parse failed: ") + e.what();
        return layout_tree;
    }
    if (!slots.is_object()) {
        if (out_error) *out_error = "slots JSON must be an object";
        return layout_tree;
    }

    std::string out;
    out.reserve(layout_tree.size());
    size_t i = 0;
    const size_t n = layout_tree.size();
    while (i < n) {
        // Look for `<%`.
        if (i + 1 < n && layout_tree[i] == '<' && layout_tree[i + 1] == '%') {
            // Find closing `%>`.
            size_t end = i + 2;
            while (end + 1 < n
                   && !(layout_tree[end] == '%' && layout_tree[end + 1] == '>')) {
                ++end;
            }
            if (end + 1 >= n) {
                // No closing `%>` — append rest literally.
                out.append(layout_tree, i, std::string::npos);
                break;
            }
            std::string key = layout_tree.substr(i + 2, end - i - 2);
            // Trim whitespace.
            while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(0, 1);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            auto it = slots.find(key);
            if (it != slots.end() && it->is_string()) {
                out += it->get<std::string>();
            } else if (it != slots.end()) {
                // Non-string slot: dump as JSON (numbers, bools).
                out += it->dump();
            } else {
                // Missing slot: keep `<%key%>` literal so the gap is visible.
                out.append(layout_tree, i, end - i + 2);
            }
            i = end + 2;
        } else {
            out.push_back(layout_tree[i]);
            ++i;
        }
    }
    return out;
}

}  // namespace icmg::compress

// v1.20.0 (F5): `icmg json` — schema-only JSON viewer.
// Output keys + value TYPES (no values) — cuts 80% on large configs.
//
// Usage: icmg json [path] [--max-depth N]
//
// Input: any JSON file path or stdin.
// Output: same structure, leaf values replaced with type strings:
//   "name": "<string>" → "name": "<string>"
//   "count": 42        → "count": "<int>"
//   "items": [1,2,3]   → "items": ["<int>"]  (1 sample)
//   "nested": {...}    → recurse

#include "../../core/registry.hpp"
#include "../base_command.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <nlohmann/json.hpp>

namespace icmg::cli {

using json = nlohmann::json;

namespace {

std::string typeName(const json& v) {
    if (v.is_string())          return "<string>";
    if (v.is_boolean())         return "<bool>";
    if (v.is_number_integer())  return "<int>";
    if (v.is_number_unsigned()) return "<uint>";
    if (v.is_number_float())    return "<float>";
    if (v.is_null())            return "<null>";
    return "<unknown>";
}

// Recursively replace leaf values with their type string. Arrays show
// 1 sample element if non-empty, plus `[N]` length hint when N > 1.
json schemafy(const json& v, int depth, int max_depth) {
    if (depth > max_depth) return "<...truncated>";

    if (v.is_object()) {
        json out = json::object();
        for (auto it = v.begin(); it != v.end(); ++it) {
            out[it.key()] = schemafy(it.value(), depth + 1, max_depth);
        }
        return out;
    }
    if (v.is_array()) {
        if (v.empty()) return json::array();
        json out = json::array();
        out.push_back(schemafy(v[0], depth + 1, max_depth));
        if (v.size() > 1) {
            out.push_back("<+" + std::to_string(v.size() - 1) + " more>");
        }
        return out;
    }
    return typeName(v);
}

} // namespace

class JsonCommand : public BaseCommand {
public:
    std::string name()        const override { return "json"; }
    std::string description() const override {
        return "Schema-only JSON viewer (keys + value types, no values)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg json [path] [--max-depth N]\n\n"
            "Reads JSON file (or stdin) and prints structure with leaf\n"
            "values replaced by type strings. Cuts 80% tokens on large configs.\n\n"
            "Options:\n"
            "  --max-depth N    Truncate beyond depth N (default 8)\n\n"
            "Examples:\n"
            "  icmg json package.json\n"
            "  curl -s api/list | icmg json\n"
            "  icmg json schema.json --max-depth 4\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }

        int max_depth = 8;
        try { max_depth = std::stoi(flagValue(args, "--max-depth", "8")); } catch (...) {}

        // Positional: first non-flag arg → file path; else stdin.
        std::string path;
        static const std::vector<std::string> valued = {"--max-depth"};
        for (size_t i = 0; i < args.size(); ++i) {
            const auto& a = args[i];
            if (a.empty()) continue;
            if (a[0] == '-') {
                for (auto& v : valued) if (a == v) { ++i; break; }
                continue;
            }
            path = a; break;
        }

        std::string raw;
        if (!path.empty()) {
            std::ifstream f(path);
            if (!f) {
                std::cerr << "icmg json: cannot open: " << path << "\n";
                return 1;
            }
            std::stringstream ss;
            ss << f.rdbuf();
            raw = ss.str();
        } else {
            std::stringstream ss;
            ss << std::cin.rdbuf();
            raw = ss.str();
        }

        if (raw.empty()) {
            std::cerr << "icmg json: empty input\n";
            return 1;
        }

        json input;
        try {
            input = json::parse(raw);
        } catch (const std::exception& e) {
            std::cerr << "icmg json: parse error: " << e.what() << "\n";
            return 1;
        }

        json schema = schemafy(input, 0, max_depth);
        std::cout << schema.dump(2) << "\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("json", JsonCommand);

} // namespace icmg::cli

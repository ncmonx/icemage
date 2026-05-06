#pragma once
#include "../core/db.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace icmg::mcp {

using json = nlohmann::json;

// MCP error with JSON-RPC error code
class McpError : public std::runtime_error {
public:
    int code;
    McpError(int code, const std::string& msg)
        : std::runtime_error(msg), code(code) {}
};

struct McpToolParam {
    std::string name;
    std::string type;        // string|number|boolean|integer
    std::string description;
    bool required = false;
};

class BaseMcpTool {
public:
    virtual ~BaseMcpTool() = default;
    virtual std::string name()        const = 0;
    virtual std::string description() const = 0;
    virtual std::vector<McpToolParam> params() const = 0;

    // A4: all tools return JSON; call() is final, callImpl() is overridden.
    json call(const json& args, core::Db& db) {
        validateArgs(args);
        return callImpl(args, db);
    }

    // JSON Schema for tools/list
    json schema() const {
        json props = json::object();
        json required = json::array();
        for (auto& p : params()) {
            props[p.name] = {{"type", p.type}, {"description", p.description}};
            if (p.required) required.push_back(p.name);
        }
        return {
            {"type", "object"},
            {"properties", props},
            {"required", required}
        };
    }

protected:
    virtual json callImpl(const json& args, core::Db& db) = 0;

    // Default: no validation. Override to add per-tool checks.
    virtual void validateArgs(const json& /*args*/) {}

    // Helpers
    static std::string getStr(const json& args, const std::string& key,
                               const std::string& def = "") {
        if (!args.contains(key) || !args[key].is_string()) return def;
        return args[key].get<std::string>();
    }
    static int getInt(const json& args, const std::string& key, int def = 10) {
        if (!args.contains(key)) return def;
        if (args[key].is_number_integer()) return args[key].get<int>();
        if (args[key].is_string()) {
            try { return std::stoi(args[key].get<std::string>()); } catch (...) {}
        }
        return def;
    }
    static void requireStr(const json& args, const std::string& key, int maxLen = 10000) {
        if (!args.contains(key) || !args[key].is_string())
            throw McpError(-32602, "Missing required param: " + key + " (string)");
        if ((int)args[key].get<std::string>().size() > maxLen)
            throw McpError(-32602, key + " exceeds " + std::to_string(maxLen) + " char limit");
    }
};

} // namespace icmg::mcp

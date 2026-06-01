// v1.8.0: `icmg hookio` — JSON helper for hook scripts, replaces jq dependency.
//
// Motivation: every hook script previously needed `jq` to parse Claude-Code
// stdin JSON (tool_name, tool_input.command, prompt) and emit envelope JSON
// ({"hookSpecificOutput":{"hookEventName":"…","additionalContext":"…"}}).
// On user machines without jq, every hook fired `command not found` errors.
// This command provides a native, dependency-free replacement.
//
// Subcommands:
//   get <dotted.path>          Read stdin JSON, output value at path (string/num)
//   emit <event> --ctx <str>   Emit {hookSpecificOutput:{hookEventName, additionalContext}}
//   emit <event> --deny <reason>  PreToolUse deny envelope
//   escape                     Read stdin → output as JSON-quoted string (jq -Rs .)
//
// JSON parser is intentionally minimal (objects + strings + numbers + nested
// objects). No arrays — hooks don't need them. Robust to unknown extra fields.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/inject_dedup.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace icmg::cli {

namespace {

// Read all of stdin into a std::string.
std::string slurpStdin() {
    std::ostringstream ss;
    ss << std::cin.rdbuf();
    return ss.str();
}

// Skip whitespace.
inline void skipWs(const std::string& s, size_t& i) {
    while (i < s.size() && (s[i]==' '||s[i]=='\t'||s[i]=='\n'||s[i]=='\r')) ++i;
}

// Parse a JSON string starting at `s[i] == '"'`. Advances `i` past closing
// quote. Returns unescaped value.
std::string parseString(const std::string& s, size_t& i) {
    std::string out;
    if (i >= s.size() || s[i] != '"') return out;
    ++i;
    while (i < s.size()) {
        char c = s[i++];
        if (c == '"') break;
        if (c == '\\' && i < s.size()) {
            char n = s[i++];
            switch (n) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"';  break;
                case '\\': out += '\\'; break;
                case '/': out += '/';  break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    // \uXXXX — only handle BMP; emit UTF-8.
                    if (i + 4 <= s.size()) {
                        unsigned cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            char h = s[i++];
                            cp <<= 4;
                            if      (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                        }
                        if (cp < 0x80) {
                            out += (char)cp;
                        } else if (cp < 0x800) {
                            out += (char)(0xC0 | (cp >> 6));
                            out += (char)(0x80 | (cp & 0x3F));
                        } else {
                            out += (char)(0xE0 | (cp >> 12));
                            out += (char)(0x80 | ((cp >> 6) & 0x3F));
                            out += (char)(0x80 | (cp & 0x3F));
                        }
                    }
                    break;
                }
                default: out += n; break;
            }
            continue;
        }
        out += c;
    }
    return out;
}

// Skip a value (string/number/bool/null/object/array). Advances `i` past it.
void skipValue(const std::string& s, size_t& i) {
    skipWs(s, i);
    if (i >= s.size()) return;
    char c = s[i];
    if (c == '"') { (void)parseString(s, i); return; }
    if (c == '{' || c == '[') {
        char open = c, close = (c == '{' ? '}' : ']');
        int depth = 0;
        while (i < s.size()) {
            char x = s[i++];
            if (x == '"') { --i; (void)parseString(s, i); continue; }
            if (x == open) ++depth;
            else if (x == close) { if (--depth == 0) return; }
        }
        return;
    }
    // number / true / false / null
    while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' &&
           s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') ++i;
}

// Within object scope (after the opening `{`), find `"key"` at top level.
// On success, sets `i` to position of value (just past `:`) and returns true.
bool findKey(const std::string& s, size_t& i, const std::string& key) {
    int depth = 0;
    while (i < s.size()) {
        skipWs(s, i);
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '}') return false;
        if (c == ',') { ++i; continue; }
        if (c != '"') { ++i; continue; }
        size_t before = i;
        std::string k = parseString(s, i);
        skipWs(s, i);
        if (i >= s.size() || s[i] != ':') { i = before + 1; continue; }
        ++i;  // past colon
        skipWs(s, i);
        if (depth == 0 && k == key) return true;
        skipValue(s, i);
    }
    return false;
}

// Resolve dotted path within s. Path leading dot optional.
// Returns extracted string (or stringified number/bool). Empty if not found.
std::string getPath(const std::string& s, const std::string& path) {
    // Split path on '.'
    std::vector<std::string> parts;
    std::string cur;
    for (char c : path) {
        if (c == '.') { if (!cur.empty()) parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) parts.push_back(cur);
    if (parts.empty()) return "";

    size_t i = 0;
    skipWs(s, i);
    if (i >= s.size() || s[i] != '{') return "";
    ++i;  // enter top object

    for (size_t p = 0; p < parts.size(); ++p) {
        if (!findKey(s, i, parts[p])) return "";
        skipWs(s, i);
        if (p + 1 == parts.size()) {
            // Final — extract value.
            if (i >= s.size()) return "";
            char c = s[i];
            if (c == '"') return parseString(s, i);
            // number/bool/null
            size_t start = i;
            while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' &&
                   s[i] != ' ' && s[i] != '\t' && s[i] != '\n' && s[i] != '\r') ++i;
            std::string raw = s.substr(start, i - start);
            if (raw == "null") return "";
            return raw;
        }
        // Descend — must be object.
        if (i >= s.size() || s[i] != '{') return "";
        ++i;  // enter nested object
    }
    return "";
}

// Escape a raw string into a JSON string literal (without surrounding quotes
// when raw=true; with quotes when raw=false).
std::string jsonEscape(const std::string& in, bool with_quotes = true) {
    std::string out;
    if (with_quotes) out += '"';
    for (unsigned char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += (char)c;
                }
        }
    }
    if (with_quotes) out += '"';
    return out;
}

}  // namespace

class HookIoCommand : public BaseCommand {
public:
    std::string name()        const override { return "hookio"; }
    std::string description() const override {
        return "JSON helper for hook scripts (replaces jq)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg hookio <action> [args]\n\n"
            "Actions:\n"
            "  get <dotted.path>            Read stdin JSON, print value at path\n"
            "  emit <event> --ctx <str>     Emit additionalContext envelope\n"
            "  emit <event> --deny <reason> Emit PreToolUse deny envelope\n"
            "  escape                       Read stdin → JSON-quoted string\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];

        if (sub == "get") {
            if (args.size() < 2) {
                std::cerr << "icmg hookio get: missing <path>\n";
                return 1;
            }
            std::string raw = slurpStdin();
            std::string path = args[1];
            if (!path.empty() && path[0] == '.') path = path.substr(1);
            std::cout << getPath(raw, path);
            return 0;
        }

        if (sub == "emit") {
            if (args.size() < 2) {
                std::cerr << "icmg hookio emit: missing <event>\n";
                return 1;
            }
            std::string event = args[1];
            std::string ctx, deny_reason;
            std::string updated_input_raw;
            bool has_ctx = false, has_deny = false;
            for (size_t i = 2; i < args.size(); ++i) {
                if (args[i] == "--ctx" && i + 1 < args.size()) {
                    ctx = args[++i]; has_ctx = true;
                } else if (args[i] == "--ctx-stdin") {
                    ctx = slurpStdin(); has_ctx = true;
                } else if (args[i] == "--deny" && i + 1 < args.size()) {
                    deny_reason = args[++i]; has_deny = true;
                }
            }
            // v1.14.0: dedup. If exact same ctx already injected this
            // session, skip — LLM still has it from prior turn. Caller
            // opts out via ICMG_NO_DEDUP=1 env or --no-dedup flag.
            bool no_dedup = hasFlag(args, "--no-dedup");
            const char* env_off = std::getenv("ICMG_NO_DEDUP");
            if (env_off && (env_off[0]=='1'||env_off[0]=='y'||env_off[0]=='Y')) {
                no_dedup = true;
            }
            if (has_ctx && !no_dedup && !has_deny &&
                core::inject_dedup::seenBefore(ctx)) {
                // Emit empty envelope — hook contract still satisfied.
                std::cout << "{\"hookSpecificOutput\":{\"hookEventName\":"
                          << jsonEscape(event) << "}}";
                return 0;
            }
            std::cout << "{\"hookSpecificOutput\":{\"hookEventName\":"
                      << jsonEscape(event);
            if (has_deny) {
                std::cout << ",\"permissionDecision\":\"deny\""
                          << ",\"permissionDecisionReason\":"
                          << jsonEscape(deny_reason);
            }
            if (has_ctx) {
                std::cout << ",\"additionalContext\":" << jsonEscape(ctx);
            }
            std::cout << "}}";
            return 0;
        }

        if (sub == "escape") {
            std::string raw = slurpStdin();
            std::cout << jsonEscape(raw);
            return 0;
        }

        std::cerr << "icmg hookio: unknown action '" << sub << "'\n";
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("hookio", HookIoCommand);

}  // namespace icmg::cli

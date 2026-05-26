#include "warm_client.hpp"
#include "warm_pipe.hpp"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <regex>

namespace icmg::llm {

std::string warmPipeName() {
    if (const char* e = std::getenv("ICMG_LLM_WARM_PIPE")) return e;
    return "icmg-llm-warm";
}

namespace {

std::string genUuid() {
    std::mt19937_64 rng{std::random_device{}()};
    char buf[37];
    std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                  (unsigned)rng(), (unsigned)(rng() & 0xffff),
                  (unsigned)(rng() & 0xffff), (unsigned)(rng() & 0xffff),
                  (unsigned long long)(rng() & 0xffffffffffffULL));
    return buf;
}

std::string escapeJson(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if ((unsigned char)c < 0x20) {
                    char b[8]; std::snprintf(b, sizeof(b), "\\u%04x", (unsigned)c); o += b;
                } else o += c;
        }
    }
    return o;
}

std::string extractField(const std::string& json, const std::string& key,
                          bool is_string) {
    if (is_string) {
        std::regex r("\"" + key + "\"\\s*:\\s*\"((?:[^\"\\\\]|\\\\.)*)\"");
        std::smatch m;
        if (std::regex_search(json, m, r)) return m[1].str();
        return {};
    }
    std::regex r("\"" + key + "\"\\s*:\\s*(-?\\d+)");
    std::smatch m;
    if (std::regex_search(json, m, r)) return m[1].str();
    return "0";
}

}  // anon

bool warmAvailable() {
    if (std::getenv("ICMG_NO_WARM_LOOP")) return false;
    auto c = PipeClient::connect(warmPipeName(), std::chrono::milliseconds(50));
    if (!c) return false;
    auto resp = c->sendRequest(R"({"id":"probe","cmd":"ping"})",
                                std::chrono::milliseconds(500));
    return resp.find("\"ok\":true") != std::string::npos;
}

std::optional<WarmInferResult> tryWarmInfer(
    const std::string& prompt,
    const InferParams& opts,
    std::chrono::milliseconds connect_timeout)
{
    if (std::getenv("ICMG_NO_WARM_LOOP")) return std::nullopt;
    auto c = PipeClient::connect(warmPipeName(), connect_timeout);
    if (!c) return std::nullopt;

    std::string req = std::string{"{\"id\":\""} + genUuid() +
        "\",\"cmd\":\"infer\",\"prompt\":\"" + escapeJson(prompt) +
        "\",\"max_tokens\":" + std::to_string(opts.max_tokens) +
        ",\"temperature\":" + std::to_string(opts.temperature) +
        ",\"top_p\":" + std::to_string(opts.top_p) +
        ",\"repeat_penalty\":" + std::to_string(opts.repeat_penalty);
    if (!opts.stop.empty())
        req += ",\"stop\":\"" + escapeJson(opts.stop) + "\"";
    req += "}";

    auto resp = c->sendRequest(req, std::chrono::milliseconds(60000));
    if (resp.empty() || resp.find("\"ok\":true") == std::string::npos)
        return std::nullopt;

    WarmInferResult r;
    r.text     = extractField(resp, "text", true);
    r.tok_in   = std::stoi(extractField(resp, "tok_in", false));
    r.tok_out  = std::stoi(extractField(resp, "tok_out", false));
    r.wall_ms  = std::stoi(extractField(resp, "wall_ms", false));
    return r;
}

}  // namespace icmg::llm

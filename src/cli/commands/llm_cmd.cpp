// v1.31.0 A3+A4+A7: `icmg llm <subcmd>` — local LLM management.
//
// Subcommands:
//   install <model>           — Download (or sideload via --path) + verify SHA256.
//   list                      — Show registry + which are downloaded + which is active.
//   use <model>               — Set active model (writes ~/.icmg/llm/active).
//   remove <model>            — Delete the .gguf from disk.
//   bench [model]             — Single-shot 64-tok generation, prints tok/s + wall.
//   status                    — RAM / threshold / active model / availability.
//   disable                   — Persist opt-out (writes ~/.icmg/llm/disabled).
//   enable                    — Clear opt-out.
//
// Storage layout:
//   ~/.icmg/llm/registry.json    — curated catalog (embedded default below; user-editable)
//   ~/.icmg/llm/active           — single line: model id
//   ~/.icmg/llm/disabled         — sentinel file (present = LLM off globally)
//   ~/.icmg/llm/<id>/model.gguf  — downloaded weights
//
// Privacy opt-out (A7): when `~/.icmg/llm/disabled` exists, `LlamaRunner::available()`
// stays true (build flag) but smart router (B1.5) MUST treat as unavailable.
// Helper `llmDisabled()` exported below.
#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/sys_resources.hpp"
#include "../../llm/llama_runner.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

namespace {

// Embedded default registry. First-launch writes this to disk if registry
// file is missing. Curated for size/license/quality. SHA256 placeholders are
// authoritative — never auto-update without explicit user `--refresh-catalog`.
constexpr const char* DEFAULT_REGISTRY_JSON = R"JSON({
  "schema_version": 1,
  "default": "qwen2.5-0.5b-q4",
  "models": [
    {
      "id": "qwen2.5-0.5b-q4",
      "name": "Qwen2.5 0.5B Instruct (Q4_K_M)",
      "url": "https://huggingface.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF/resolve/main/qwen2.5-0.5b-instruct-q4_k_m.gguf",
      "sha256": "PENDING_FILL_ON_PUBLISH",
      "size_mb": 398,
      "min_ram_mb": 1536,
      "license": "Apache-2.0",
      "context": 32768,
      "language": "multilingual"
    },
    {
      "id": "qwen2.5-1.5b-q4",
      "name": "Qwen2.5 1.5B Instruct (Q4_K_M)",
      "url": "https://huggingface.co/Qwen/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/qwen2.5-1.5b-instruct-q4_k_m.gguf",
      "sha256": "PENDING_FILL_ON_PUBLISH",
      "size_mb": 986,
      "min_ram_mb": 2560,
      "license": "Apache-2.0",
      "context": 32768,
      "language": "multilingual"
    }
  ]
})JSON";

fs::path llmDir() {
    const char* home =
#ifdef _WIN32
        std::getenv("USERPROFILE");
#else
        std::getenv("HOME");
#endif
    fs::path p = (home && *home) ? fs::path(home) : fs::current_path();
    p /= ".icmg"; p /= "llm";
    std::error_code ec;
    fs::create_directories(p, ec);
    return p;
}

void ensureRegistry(const fs::path& dir) {
    fs::path reg = dir / "registry.json";
    if (fs::exists(reg)) return;
    std::ofstream(reg) << DEFAULT_REGISTRY_JSON;
}

bool fileHasContent(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && fs::file_size(p, ec) > 0;
}

std::string readActive(const fs::path& dir) {
    std::ifstream f(dir / "active");
    if (!f) return "";
    std::string s; std::getline(f, s);
    return s;
}

void writeActive(const fs::path& dir, const std::string& id) {
    std::ofstream(dir / "active") << id;
}

bool llmDisabled(const fs::path& dir) {
    std::error_code ec;
    return fs::exists(dir / "disabled", ec);
}

int cmdStatus() {
    fs::path dir = llmDir();
    ensureRegistry(dir);
    std::cout << "icmg llm status\n";
    std::cout << "  build:        " << (llm::LlamaRunner::available() ? "ICMG_USE_LLAMA=ON"
                                                                       : "ICMG_USE_LLAMA=OFF (LLM stubbed)") << "\n";
    std::cout << "  user opt-out: " << (llmDisabled(dir) ? "YES (toggle: icmg llm enable)" : "no") << "\n";
    std::cout << "  total RAM:    " << core::totalRamMB()     << " MB\n";
    std::cout << "  available:    " << core::availableRamMB() << " MB\n";
    std::cout << "  threshold:    " << core::llmMinRamThresholdMB() << " MB (override: ICMG_LLM_MIN_RAM_MB)\n";
    std::cout << "  ram ok:       " << (core::llmHasEnoughRam() ? "yes" : "NO") << "\n";
    std::string active = readActive(dir);
    std::cout << "  active model: " << (active.empty() ? "(none)" : active) << "\n";
    std::cout << "  registry:     " << (dir / "registry.json").string() << "\n";
    return 0;
}

int cmdList() {
    fs::path dir = llmDir();
    ensureRegistry(dir);
    std::ifstream f(dir / "registry.json");
    std::stringstream ss; ss << f.rdbuf();
    std::cout << ss.str() << "\n";
    std::string active = readActive(dir);
    std::cout << "\nactive: " << (active.empty() ? "(none)" : active) << "\n";
    return 0;
}

int cmdDisable() {
    fs::path dir = llmDir();
    std::ofstream(dir / "disabled") << "user opt-out v1.31.0\n";
    std::cout << "icmg llm disable: opt-out persisted. Smart router will skip LLM.\n";
    return 0;
}

int cmdEnable() {
    fs::path dir = llmDir();
    std::error_code ec;
    fs::remove(dir / "disabled", ec);
    std::cout << "icmg llm enable: opt-out cleared.\n";
    return 0;
}

int cmdUse(const std::vector<std::string>& args) {
    if (args.size() < 2) { std::cerr << "usage: icmg llm use <model-id>\n"; return 1; }
    fs::path dir = llmDir();
    writeActive(dir, args[1]);
    std::cout << "icmg llm use: active = " << args[1] << "\n";
    return 0;
}

int cmdInstall(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "usage: icmg llm install <model-id> [--path <local.gguf>] [--offline]\n";
        return 1;
    }
    const std::string& id = args[1];
    std::string local_path;
    bool offline = false;
    for (std::size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--path" && i + 1 < args.size()) { local_path = args[++i]; }
        else if (args[i] == "--offline") { offline = true; }
    }

    fs::path dir = llmDir();
    ensureRegistry(dir);
    fs::path model_dir = dir / id;
    std::error_code ec; fs::create_directories(model_dir, ec);
    fs::path dest = model_dir / "model.gguf";

    if (!local_path.empty()) {
        fs::copy_file(local_path, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) { std::cerr << "copy failed: " << ec.message() << "\n"; return 2; }
        std::cout << "icmg llm install: sideloaded " << local_path << " -> " << dest.string() << "\n";
        std::cout << "WARN: SHA256 verification skipped on sideload. Trust the source.\n";
        writeActive(dir, id);
        return 0;
    }

    if (offline) {
        std::cerr << "icmg llm install --offline requires --path <local.gguf>\n";
        return 1;
    }

    // A5 (HTTP download + SHA256 verify): not yet wired. Stream-to-file
    // download primitive missing — existing `download()` in fetch_cmd.cpp
    // reads body into memory, unfit for 400 MB. Add streaming variant in
    // next iteration.
    std::cerr << "icmg llm install: HTTP download not yet wired (A5 pending).\n"
              << "  Sideload instead:\n"
              << "    icmg llm install " << id << " --path C:/path/to/model.gguf --offline\n";
    return 3;
}

int cmdRemove(const std::vector<std::string>& args) {
    if (args.size() < 2) { std::cerr << "usage: icmg llm remove <model-id>\n"; return 1; }
    fs::path dir = llmDir() / args[1];
    std::error_code ec;
    auto removed = fs::remove_all(dir, ec);
    if (ec) { std::cerr << "remove failed: " << ec.message() << "\n"; return 2; }
    std::cout << "icmg llm remove: removed " << removed << " entries under " << dir.string() << "\n";
    return 0;
}

int cmdBench(const std::vector<std::string>& args) {
    fs::path dir = llmDir();
    std::string id = (args.size() >= 2) ? args[1] : readActive(dir);
    if (id.empty()) { std::cerr << "no active model — `icmg llm use <id>` first\n"; return 1; }
    fs::path gguf = dir / id / "model.gguf";
    if (!fileHasContent(gguf)) {
        std::cerr << "model not installed: " << gguf.string() << "\n";
        return 2;
    }
    if (!llm::LlamaRunner::available()) {
        std::cerr << "build lacks ICMG_USE_LLAMA — bench unavailable. Rebuild with -DICMG_USE_LLAMA=ON.\n";
        return 3;
    }
    llm::LlamaRunner r;
    if (!r.load(gguf.string())) {
        std::cerr << "load failed: " << r.lastError() << "\n";
        return 4;
    }
    llm::InferParams ip; ip.max_tokens = 64; ip.temperature = 0.0f;
    auto res = r.infer("Summarize in one sentence: icmg is a CLI for token-efficient AI coding assistance.", ip);
    if (!res.ok) { std::cerr << "infer failed: " << res.error << "\n"; return 5; }
    std::cout << "bench " << id << ":\n";
    std::cout << "  prompt:   " << res.tokens_in  << " tokens\n";
    std::cout << "  output:   " << res.tokens_out << " tokens\n";
    std::cout << "  wall:     " << res.wall_ms << " ms\n";
    if (res.wall_ms > 0)
        std::cout << "  tok/s:    " << (res.tokens_out * 1000.0 / res.wall_ms) << "\n";
    std::cout << "---\n" << res.text << "\n";
    return 0;
}

} // namespace

// Exported for B1.5 smart router (next iteration).
bool isLlmUserDisabled() { return llmDisabled(llmDir()); }
std::string activeLlmModelId() { return readActive(llmDir()); }

class LlmCommand : public BaseCommand {
public:
    std::string name()        const override { return "llm"; }
    std::string description() const override { return "Manage local LLMs (install / use / bench / status)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg llm <subcommand> [args]\n\n"
            "Subcommands:\n"
            "  install <id> [--path P] [--offline]   Download (A5 pending) or sideload .gguf\n"
            "  list                                  Show registry + active selection\n"
            "  use <id>                              Set active model\n"
            "  remove <id>                           Delete model from disk\n"
            "  bench [id]                            64-tok benchmark with tok/s\n"
            "  status                                Build flag + RAM + opt-out + active\n"
            "  disable                               Persist privacy opt-out\n"
            "  enable                                Clear opt-out\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") { usage(); return 0; }
        const std::string& sub = args[0];
        if (sub == "status")  return cmdStatus();
        if (sub == "list")    return cmdList();
        if (sub == "use")     return cmdUse(args);
        if (sub == "install") return cmdInstall(args);
        if (sub == "remove")  return cmdRemove(args);
        if (sub == "bench")   return cmdBench(args);
        if (sub == "disable") return cmdDisable();
        if (sub == "enable")  return cmdEnable();
        std::cerr << "icmg llm: unknown subcommand '" << sub << "'. Try `icmg llm --help`.\n";
        return 1;
    }
};

ICMG_REGISTER_COMMAND("llm", LlmCommand);

} // namespace icmg::cli

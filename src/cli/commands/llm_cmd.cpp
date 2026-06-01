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
#include "../../core/http_stream.hpp"
#include "../../llm/llama_runner.hpp"
#include "../../llm/telemetry.hpp"
#include "../../llm/warm_pool.hpp"
// v1.52.0: cross-process warm-pipe fast path.
#include "../../llm/warm_client.hpp"
#include "../../llm/chat_template.hpp"
#include "../../core/persona_loader.hpp"

#include <nlohmann/json.hpp>
#include "../llm_list_json.hpp"   // v1.70.0 #177
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg::cli {

// Forward declarations for warm subcommands (implemented in llm_warm_cmd.cpp).
int runLlmWarm(const std::vector<std::string>&);
int runLlmWarmLoop(const std::vector<std::string>&);

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
    },
    {
      "id": "gemma-2-2b-q4",
      "name": "Gemma 2 2B Instruct (Q4_K_M)",
      "url": "https://huggingface.co/bartowski/gemma-2-2b-it-GGUF/resolve/main/gemma-2-2b-it-Q4_K_M.gguf",
      "sha256": "PENDING_FILL_ON_PUBLISH",
      "size_mb": 1635,
      "min_ram_mb": 3072,
      "license": "Gemma-Terms",
      "context": 8192,
      "language": "multilingual"
    },
    {
      "id": "llama-3.2-3b-q4",
      "name": "Llama 3.2 3B Instruct (Q4_K_M)",
      "url": "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q4_K_M.gguf",
      "sha256": "PENDING_FILL_ON_PUBLISH",
      "size_mb": 2020,
      "min_ram_mb": 4096,
      "license": "Llama-3.2-Community",
      "context": 131072,
      "language": "multilingual"
    },
    {
      "id": "phi-3.5-mini-q4",
      "name": "Phi-3.5 Mini 3.8B Instruct (Q4_K_M)",
      "url": "https://huggingface.co/bartowski/Phi-3.5-mini-instruct-GGUF/resolve/main/Phi-3.5-mini-instruct-Q4_K_M.gguf",
      "sha256": "PENDING_FILL_ON_PUBLISH",
      "size_mb": 2393,
      "min_ram_mb": 4608,
      "license": "MIT",
      "context": 131072,
      "language": "multilingual"
    },
    {
      "id": "qwen2.5-7b-q4",
      "name": "Qwen2.5 7B Instruct (Q4_K_M)",
      "url": "https://huggingface.co/bartowski/Qwen2.5-7B-Instruct-GGUF/resolve/main/Qwen2.5-7B-Instruct-Q4_K_M.gguf",
      "sha256": "PENDING_FILL_ON_PUBLISH",
      "size_mb": 4683,
      "min_ram_mb": 8192,
      "license": "Apache-2.0",
      "context": 32768,
      "language": "multilingual",
      "gpu_recommended": true
    },
    {
      "id": "mistral-7b-v0.3-q4",
      "name": "Mistral 7B Instruct v0.3 (Q4_K_M)",
      "url": "https://huggingface.co/bartowski/Mistral-7B-Instruct-v0.3-GGUF/resolve/main/Mistral-7B-Instruct-v0.3-Q4_K_M.gguf",
      "sha256": "PENDING_FILL_ON_PUBLISH",
      "size_mb": 4368,
      "min_ram_mb": 8192,
      "license": "Apache-2.0",
      "context": 32768,
      "language": "multilingual",
      "gpu_recommended": true
    },
    {
      "id": "llama-3.1-8b-q4",
      "name": "Llama 3.1 8B Instruct (Q4_K_M)",
      "url": "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/main/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",
      "sha256": "PENDING_FILL_ON_PUBLISH",
      "size_mb": 4920,
      "min_ram_mb": 8704,
      "license": "Llama-3.1-Community",
      "context": 131072,
      "language": "multilingual",
      "gpu_recommended": true
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

// v1.31.0 B2: first-launch consent. Returns true if user has consented
// (sentinel exists or just granted). False if user declines or input
// stream unavailable (non-interactive).
//   --yes flag bypasses prompt (CI / scripted installs).
//   ICMG_LLM_CONSENT=1 env also bypasses (non-interactive shells).
bool ensureConsent(const fs::path& dir, bool yes_flag) {
    std::error_code ec;
    if (fs::exists(dir / "consent", ec)) return true;
    if (yes_flag) {
        std::ofstream(dir / "consent") << "granted via --yes flag\n";
        return true;
    }
    if (const char* e = std::getenv("ICMG_LLM_CONSENT"); e && *e == '1') {
        std::ofstream(dir / "consent") << "granted via ICMG_LLM_CONSENT=1\n";
        return true;
    }
    std::cout <<
        "\nicmg llm — first-launch consent\n"
        "================================\n"
        "About to download a local LLM model (400 MB - 2 GB) to:\n"
        "  " << dir.string() << "\n"
        "\n"
        "What happens after install:\n"
        "  - Model runs LOCALLY on your CPU/Vulkan. No data leaves your machine.\n"
        "  - Used opt-in for: ask --backend=local, pack --rerank, PreCompact summarize.\n"
        "  - Disable any time:  `icmg llm disable`\n"
        "  - Remove model:      `icmg llm remove <id>`\n"
        "\n"
        "Resources used while active:\n"
        "  - RAM ~1.5 GB (Qwen 0.5B) or ~2.5 GB (Qwen 1.5B)\n"
        "  - Disk ~400 MB - 2 GB per model\n"
        "  - CPU during inference (warm/cold paths only; never hot-path)\n"
        "\n"
        "Proceed? [y/N]: " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) {
        std::cerr << "icmg llm: stdin closed; aborting. Use `--yes` for non-interactive.\n";
        return false;
    }
    if (!line.empty() && (line[0] == 'y' || line[0] == 'Y')) {
        std::ofstream(dir / "consent") << "granted interactively\n";
        std::cout << "Consent recorded. To revoke: `icmg llm revoke-consent`.\n";
        return true;
    }
    std::cout << "Aborted. No model downloaded. Re-run when ready.\n";
    return false;
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
    // B5 telemetry summary.
    auto st = llm::Telemetry::instance().stats(10);
    std::cout << "  telemetry (last " << st.n << "):\n";
    std::cout << "    p50 wall:        " << static_cast<int>(st.p50_wall_ms) << " ms\n";
    std::cout << "    p95 wall:        " << static_cast<int>(st.p95_wall_ms) << " ms\n";
    std::cout << "    avg tok/s:       " << static_cast<int>(st.avg_tok_per_s) << "\n";
    std::cout << "    error rate:      " << static_cast<int>(st.error_rate * 100) << "%\n";
    std::cout << "    cold-load fails: " << st.cold_load_fail_count << "\n";
    std::cout << "  warm-pool:    " << (llm::WarmPool::instance().isLoaded() ? "loaded" : "cold") << "\n";
    return 0;
}

int cmdList() {
    fs::path dir = llmDir();
    ensureRegistry(dir);
    std::ifstream f(dir / "registry.json");
    std::stringstream ss; ss << f.rdbuf();
    std::string active = readActive(dir);
    // v1.70.0 #177: emit a single pure-JSON document with an "active" key
    // (no trailing plain-text line that breaks json::parse).
    std::cout << buildLlmListJson(ss.str(), active) << "\n";
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

int cmdRevokeConsent() {
    fs::path dir = llmDir();
    std::error_code ec;
    bool had = fs::remove(dir / "consent", ec);
    std::cout << "icmg llm revoke-consent: "
              << (had ? "removed consent sentinel. Next install re-prompts.\n"
                     : "no consent sentinel to remove.\n");
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
    bool yes = false;
    for (std::size_t i = 2; i < args.size(); ++i) {
        if (args[i] == "--path" && i + 1 < args.size()) { local_path = args[++i]; }
        else if (args[i] == "--offline") { offline = true; }
        else if (args[i] == "--yes" || args[i] == "-y") { yes = true; }
    }

    fs::path dir = llmDir();
    ensureRegistry(dir);
    if (!ensureConsent(dir, yes)) return 10;
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

    // v1.31.0 A5b: HTTP streaming download via core::downloadToFile (system curl)
    // + SHA256 verify. Lookup model entry in registry.json.
    std::ifstream reg_in(dir / "registry.json");
    if (!reg_in) {
        std::cerr << "icmg llm install: registry.json missing — run `icmg llm list` once first.\n";
        return 4;
    }
    nlohmann::json reg;
    try { reg_in >> reg; }
    catch (const std::exception& e) {
        std::cerr << "icmg llm install: registry.json parse: " << e.what() << "\n";
        return 4;
    }
    const auto& models = reg.value("models", nlohmann::json::array());
    std::string url, expected_sha;
    std::uint64_t min_ram_mb = 0;
    for (const auto& m : models) {
        if (m.value("id", std::string{}) == id) {
            url          = m.value("url", std::string{});
            expected_sha = m.value("sha256", std::string{});
            min_ram_mb   = m.value("min_ram_mb", static_cast<std::uint64_t>(0));
            break;
        }
    }
    if (url.empty()) {
        std::cerr << "icmg llm install: model '" << id << "' not in registry. `icmg llm list` to see ids.\n";
        return 4;
    }

    // RAM pre-check — refuse before downloading 400 MB if host can't run it.
    if (!core::llmHasEnoughRam(min_ram_mb)) {
        std::cerr << "icmg llm install: RAM guard refuse (avail="
                  << core::availableRamMB() << " MB < need="
                  << core::llmMinRamThresholdMB(min_ram_mb) << " MB)\n";
        return 7;
    }

    std::cout << "icmg llm install " << id << ": downloading " << url << "\n";
    std::cout << "  size hint: " << reg["models"][0].value("size_mb", 0) << " MB (approx)\n";

    int last_pct = -1;
    auto on_progress = [&](std::uint64_t got, std::uint64_t /*total*/) -> bool {
        // We don't know total reliably without HEAD pre-request; print MB.
        int mb = static_cast<int>(got / (1024 * 1024));
        if (mb != last_pct) {
            std::cout << "  ... " << mb << " MB\r" << std::flush;
            last_pct = mb;
        }
        return true;
    };
    auto dl = core::downloadToFile(url, dest.string(), /*timeout_s=*/1200, on_progress);
    std::cout << "\n";
    if (!dl.ok) {
        std::cerr << "icmg llm install: download failed: " << dl.error << "\n";
        return 8;
    }
    std::cout << "  downloaded " << (dl.bytes / (1024 * 1024)) << " MB in "
              << static_cast<int>(dl.wall_ms / 1000) << " s\n";

    if (expected_sha.empty() || expected_sha == "PENDING_FILL_ON_PUBLISH") {
        std::cout << "  SHA256: skipped (registry entry has no published hash).\n";
        std::cout << "  WARN: integrity unverified — re-run after publish to validate.\n";
    } else {
        std::cout << "  verifying SHA256...\n";
        if (!core::verifySha256(dest.string(), expected_sha)) {
            std::cerr << "icmg llm install: SHA256 MISMATCH. Deleting tampered/corrupted file.\n";
            std::error_code _e; fs::remove(dest, _e);
            return 9;
        }
        std::cout << "  SHA256 OK.\n";
    }

    writeActive(dir, id);
    std::cout << "  active model -> " << id << "\n";
    return 0;
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


int cmdRespond(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "usage: icmg llm respond <prompt...>\n";
        return 1;
    }
    std::string user;
    for (size_t i = 1; i < args.size(); ++i) {
        if (i > 1) user += ' ';
        user += args[i];
    }
    if (!llm::LlamaRunner::available()) {
        std::cerr << "build lacks ICMG_USE_LLAMA — respond unavailable.\n";
        return 2;
    }
    // v1.53.0: if daemon owns model, skip in-process WarmPool::acquire (would RAM-refuse).
    std::string sys = std::getenv("ICMG_NO_PERSONA")
                          ? std::string{}
                          : icmg::core::buildPersonaPrefix();
    std::string prompt = icmg::llm::buildChatMLPrompt(sys, user);
    llm::InferParams ip;
    ip.max_tokens  = 200;
    ip.temperature = 0.7f;
    ip.stop        = icmg::llm::chatMLStopToken();
    if (icmg::llm::warmAvailable()) {
        // Daemon visible: route through warm-pipe only; in-process acquire would fail.
        if (auto warm = icmg::llm::tryWarmInfer(prompt, ip,
                            std::chrono::milliseconds(100));
            warm) {
            std::cout << warm->text << "\n";
            return 0;
        }
        std::cerr << "icmg llm respond: warm daemon visible but request failed\n";
        return 3;
    }
    std::string err;
    auto* run = llm::WarmPool::instance().acquire(err);
    if (!run) {
        std::cerr << "icmg llm respond: warm-pool acquire failed: " << err << "\n";
        return 3;
    }
    auto res = run->infer(prompt, ip);
    if (!res.ok) {
        std::cerr << "icmg llm respond: infer failed: " << res.error << "\n";
        return 4;
    }
    bool hook_mode = false;
    for (const auto& a : args) if (a == "--hook") { hook_mode = true; break; }
    if (hook_mode) {
        // Emit UserPromptSubmit block envelope. Claude API skipped; user
        // sees `reason` text in transcript as the assistant turn.
        std::string esc;
        esc.reserve(res.text.size() + 16);
        for (char c : res.text) {
            switch (c) {
                case '"':  esc += "\\\""; break;
                case '\\': esc += "\\\\"; break;
                case '\n': esc += "\\n";  break;
                case '\r': esc += "\\r";  break;
                case '\t': esc += "\\t";  break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        esc += buf;
                    } else {
                        esc += c;
                    }
            }
        }
        std::cout << "{\"decision\":\"block\",\"reason\":\""
                  << esc << "\"}\n";
    } else {
        std::cout << res.text << "\n";
    }
    return 0;
}

class LlmCommand : public BaseCommand {
public:
    std::string name()        const override { return "llm"; }
    std::string description() const override { return "Manage local LLMs (install / use / bench / status)"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg llm <subcommand> [args]\n\n"
            "Subcommands:\n"
            "  install <id> [--path P] [--offline] [--yes]   Download + SHA256 verify (or sideload)\n"
            "  list                                          Show registry + active selection\n"
            "  use <id>                                      Set active model\n"
            "  remove <id>                                   Delete model from disk\n"
            "  bench [id]                                    64-tok benchmark with tok/s\n"
            "  status                                        Build flag + RAM + opt-out + active\n"
            "  disable                                       Persist privacy opt-out\n"
            "  enable                                        Clear opt-out\n"
            "  respond <prompt>                             Single-turn local-LLM reply (persona+ChatML)\n"
            "  revoke-consent                                Remove first-launch consent sentinel\n";
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
        if (sub == "respond") return cmdRespond(args);
        if (sub == "revoke-consent") return cmdRevokeConsent();
        if (sub == "warm") {
            std::vector<std::string> rest(args.begin() + 1, args.end());
            return runLlmWarm(rest);
        }
        if (sub == "warm-loop") {
            std::vector<std::string> rest(args.begin() + 1, args.end());
            return runLlmWarmLoop(rest);
        }
        std::cerr << "icmg llm: unknown subcommand '" << sub << "'. Try `icmg llm --help`.\n";
        return 1;
    }
};

ICMG_REGISTER_COMMAND("llm", LlmCommand);

} // namespace icmg::cli

// Phase 23 Task 5: `icmg chat` REPL.
// Interactive loop:
//   user prompt → optional pack → LLM agent (via `icmg agent`) → print + auto-store
//
// Slash commands:
//   \save <name>   snapshot session
//   \load <name>   restore session
//   \clear         reset working context (memory remains)
//   \help          command list
//   \quit / Ctrl-D exit
//
// MVP: std::getline (no linenoise dep). History persists to ~/.icmg/chat-history.txt
// across launches via append-on-input.
//
// Without agent.command configured, chat runs in `--no-llm` sandbox: shows packed
// prompt + echoes back, useful for testing context bundles.

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../core/exec_utils.hpp"
#include "../../core/path_utils.hpp"
#include "../auto_rule.hpp"
#include <regex>
#include "../../imem/memory_store.hpp"
#include "../../imem/memory_node.hpp"
// v1.39.1 B: local LLM backend via warm-pool.
#include "../../llm/warm_pool.hpp"
#include "../../llm/llama_runner.hpp"
// v1.52.0: cross-process warm-pipe fast path.
#include "../../llm/warm_client.hpp"
#include "../../llm/chat_template.hpp"
#include "../../llm/chat_persistence.hpp"
#include "../../core/user_identity.hpp"
// v1.47.0: per-process chat history (persists across REPL turns,
// reset on \clear). Capped to last 10 turns to avoid ctx overflow.
#include <utility>
// v1.42.0: persona prefix integration.
#include "../../core/persona_loader.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <cstdlib>
#ifdef _WIN32
  #include <windows.h>
  #include <io.h>
#else
  #include <unistd.h>
#endif

namespace {

static std::vector<icmg::cli::RuleRecord> parseRuleListJson(const std::string& json) {
    std::vector<icmg::cli::RuleRecord> out;
    // Best-effort: match {"id":X,"name":"Y","content":"Z"} objects.
    // Uses separate field passes to avoid escaping issues.
    std::regex re_id(R"re("id"\s*:\s*"?([^",}\s]+)"?)re");
    std::regex re_name(R"re("name"\s*:\s*"([^"]*)")re");
    std::regex re_content(R"re("content"\s*:\s*"([^"]*)")re");
    // Split on top-level objects
    size_t pos = 0;
    while (pos < json.size()) {
        size_t ob = json.find('{', pos);
        if (ob == std::string::npos) break;
        size_t cb = json.find('}', ob);
        if (cb == std::string::npos) break;
        std::string obj = json.substr(ob, cb - ob + 1);
        std::smatch m_id, m_name, m_content;
        if (std::regex_search(obj, m_id, re_id) &&
            std::regex_search(obj, m_name, re_name) &&
            std::regex_search(obj, m_content, re_content)) {
            out.push_back({m_id[1].str(), m_name[1].str(), m_content[1].str()});
        }
        pos = cb + 1;
    }
    return out;
}

static icmg::cli::NLAdapters buildNLAdapters() {
    using namespace icmg::cli;
    NLAdapters a;
    std::string self = icmg::core::selfExePath();

    auto esc_dq = [](const std::string& s) {
        std::string out; out.reserve(s.size() + 8);
        for (char c : s) { if (c == '"') out += "\\\""; else out += c; }
        return out;
    };

    a.rule_save = [self, esc_dq](const std::string& name, const std::string& body, bool update) {
        std::string cmd = "\"" + self + "\" rule add / custom \"" + esc_dq(name) + "\" \"" + esc_dq(body) + "\"";
        if (update) cmd += " --update";
        auto r = icmg::core::safeExecShell(cmd, true, 5000);
        return r.exit_code;
    };
    a.rule_disable = [self](const std::string& id) {
        std::string cmd = "\"" + self + "\" rule disable " + id;
        auto r = icmg::core::safeExecShell(cmd, true, 5000);
        return r.exit_code;
    };
    a.rule_list = [self]() {
        std::vector<RuleRecord> out;
        std::string cmd = "\"" + self + "\" rule list --json";
        auto r = icmg::core::safeExecShell(cmd, true, 5000);
        if (r.exit_code != 0) return out;
        return parseRuleListJson(r.out);
    };
    a.skill_save = [self, esc_dq](const std::string& name, const std::string& body, bool update) {
        std::string sub = update ? "edit" : "add";
        std::string cmd = "\"" + self + "\" skill " + sub + " " + esc_dq(name) + " \"" + esc_dq(body) + "\"";
        auto r = icmg::core::safeExecShell(cmd, true, 10000);
        return r.exit_code;
    };
    a.skill_remove = [self](const std::string& name) {
        std::string cmd = "\"" + self + "\" skill remove " + name;
        auto r = icmg::core::safeExecShell(cmd, true, 5000);
        return r.exit_code;
    };
    a.skill_list = [self]() {
        std::vector<RuleRecord> out;
        std::string cmd = "\"" + self + "\" skill list --json";
        auto r = icmg::core::safeExecShell(cmd, true, 5000);
        if (r.exit_code != 0) return out;
        return parseRuleListJson(r.out);
    };
    return a;
}

} // anon ns

namespace icmg::cli {

static std::string sessionStamp();  // fwd decl

// v1.48.0 context injection: pull from past chats (BM25 cross-session)
// + project memory_nodes (BM25 within project). Cap each source 300
// chars; total budget ~1000 chars. Returns formatted block to prepend
// to system prompt. Empty when no relevant hits or budget exceeded.
static std::string buildContextInjection(
        const std::string& user_msg,
        const std::string& user_id,
        icmg::imem::MemoryStore& mem) {
    std::string out;
    // 1. Past chats BM25 (cross-session, per-user).
    try {
        auto hits = icmg::llm::bm25RecallChats(user_id, user_msg, 3);
        if (!hits.empty()) {
            out += "[Past chats]\n";
            for (const auto& h : hits) {
                std::string snippet = h.content.substr(0, 200);
                out += "- " + snippet;
                if (h.content.size() > 200) out += "...";
                out += "\n";
            }
            out += "\n";
        }
    } catch (...) {}
    // 2. Project memory_nodes BM25 recall.
    try {
        auto mhits = mem.recall(user_msg, 3);
        if (!mhits.empty()) {
            out += "[Project memory]\n";
            for (const auto& m : mhits) {
                std::string snippet = m.content.substr(0, 200);
                out += "- " + snippet;
                if (m.content.size() > 200) out += "...";
                out += "\n";
            }
            out += "\n";
        }
    } catch (...) {}
    // 3. Active rules (project rule_store via subprocess).
    try {
        auto rule_er = icmg::core::safeExecShell(
            "icmg rule list --active 2>/dev/null", true, 2000);
        auto rule_res = rule_er.out;
            if (rule_er.exit_code == 0 && !rule_res.empty() && rule_res.size() < 500) {
            out += "[Active rules]\n";
            std::string trimmed = rule_res.substr(0, 400);
            out += trimmed;
            if (rule_res.size() > 400) out += "...";
            out += "\n\n";
        }
    } catch (...) {}
    // 4. Available skills (per-project + global, via subprocess).
    try {
        auto skill_er = icmg::core::safeExecShell(
            "icmg skill list 2>/dev/null", true, 2000);
        auto skill_res = skill_er.out;
            if (skill_er.exit_code == 0 && !skill_res.empty() && skill_res.size() < 800) {
            out += "[Available skills]\n";
            std::string trimmed = skill_res.substr(0, 400);
            out += trimmed;
            if (skill_res.size() > 400) out += "...";
            out += "\n\n";
        }
    } catch (...) {}
    // Hard cap total inject 2500 chars (allowing 4 sources now).
    if (out.size() > 2500) out.resize(2500);
    return out;
}

static std::string homeDir() {
#ifdef _WIN32
    if (const char* h = std::getenv("USERPROFILE")) return h;
#endif
    if (const char* h = std::getenv("HOME")) return h;
    return ".";
}

static std::string sessionStamp() {
    auto t = std::time(nullptr);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", std::localtime(&t));
    return buf;
}

class ChatCommand : public BaseCommand {
public:
    std::string name()        const override { return "chat"; }
    std::string description() const override { return "Interactive REPL over LLM agent"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg chat [options]\n\n"
            "Options:\n"
            "  --no-llm        Sandbox mode: print packed prompt only (no LLM call)\n"
            "  --no-pack       Skip pack step on each turn\n"
            "  --session NAME  Use specific session id (default: timestamp)\n\n"
            "Slash commands inside REPL:\n"
            "  \\save <name>   Snapshot session\n"
            "  \\load <name>   Restore session\n"
            "  \\clear         Reset chat context\n"
            "  \\sessions      List recent chat sessions\n"
            "  \\resume <id>   Resume past session\n"
            "  \\new           Start fresh session\n"
            "  \\context on/off  Toggle auto-context injection\n"
            "  \\sources       Show what was injected last turn\n"
            "  \\memo <text>   Save text to project memory\n"
            "  \\help          Show this help\n"
            "  \\quit          Exit\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h")) { usage(); return 0; }

        bool no_llm  = hasFlag(args, "--no-llm");
        bool no_pack = hasFlag(args, "--no-pack");
        // v1.39.1 B: --backend=local short-circuits to warm-pool LLM,
        // skipping `icmg agent` subprocess + cloud cost entirely.
        bool local_backend = hasFlag(args, "--local") || hasFlag(args, "--backend=local");
        std::string session = flagValue(args, "--session", "");
        if (session.empty()) session = sessionStamp();

        // v1.48.0: lift state to function scope so slash cmds can mutate.
        std::vector<std::pair<std::string,std::string>> chat_history;
        bool history_seeded = false;
        bool inject_context = true;  // v1.48.1 ON (chunk-decode handles big prompts safely).
        std::string last_sources_dump;  // \sources debug

        auto& cfg = core::Config::instance();
        std::string history_path = homeDir() + "/.icmg/chat-history.txt";
        std::ofstream history(history_path, std::ios::app);

        std::cerr << "icmg chat — session=" << session
                  << (no_llm ? " (no-llm sandbox)" : "")
                  << "\n  type \\help for commands, \\quit to exit\n\n";

        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore mem(db);

        std::string self_path = locateSelf();

        std::string line;
        for (;;) {
            std::cerr << "icmg> " << std::flush;
            if (!std::getline(std::cin, line)) { std::cerr << "\n"; break; }
            if (line.empty()) continue;
            history << line << "\n"; history.flush();

            if (line[0] == '\\') {
                if (line == "\\quit" || line == "\\exit") break;
                if (line == "\\help") { usage(); continue; }
                if (line == "\\clear") {
                    std::cerr << "(working context cleared; memory preserved)\n";
                    continue;
                }
                if (line.rfind("\\save ", 0) == 0) {
                    std::string nm = line.substr(6);
                    saveSession(db, nm, session);
                    std::cerr << "saved session: " << nm << "\n";
                    continue;
                }
                if (line.rfind("\\load ", 0) == 0) {
                    std::string nm = line.substr(6);
                    auto data = loadSession(db, nm);
                    if (data.empty()) std::cerr << "no session: " << nm << "\n";
                    else std::cerr << data << "\n";
                    continue;
                }
                if (line == "\\sessions") {
                    auto sess = icmg::llm::listRecentSessions(
                        icmg::core::currentUser(), 20);
                    if (sess.empty()) std::cerr << "(no past sessions)\n";
                    for (const auto& cs : sess) {
                        std::cerr << "  " << cs.session_id
                                  << "  [" << cs.turn_count << " turns]  "
                                  << cs.preview << "\n";
                    }
                    continue;
                }
                if (line.rfind("\\resume ", 0) == 0) {
                    std::string sid = line.substr(8);
                    if (sid.empty()) { std::cerr << "usage: \\resume <session-id>\n"; continue; }
                    session = sid;
                    chat_history = icmg::llm::loadSessionHistory(
                        icmg::core::currentUser(), session, 20);
                    history_seeded = true;
                    std::cerr << "(resumed session: " << sid << " — "
                              << (chat_history.size()/2) << " turns loaded)\n";
                    continue;
                }
                if (line == "\\new") {
                    session = sessionStamp();
                    chat_history.clear();
                    history_seeded = true;  // skip re-seed for fresh session
                    std::cerr << "(new session: " << session << ")\n";
                    continue;
                }
                if (line == "\\context off") {
                    inject_context = false;
                    std::cerr << "(context injection OFF — raw chat only)\n";
                    continue;
                }
                if (line == "\\context on") {
                    inject_context = true;
                    std::cerr << "(context injection ON)\n";
                    continue;
                }
                if (line == "\\sources") {
                    if (last_sources_dump.empty())
                        std::cerr << "(no sources injected last turn)\n";
                    else
                        std::cerr << last_sources_dump << "\n";
                    continue;
                }
                if (line.rfind("\\memo ", 0) == 0) {
                    std::string txt = line.substr(6);
                    if (txt.empty()) { std::cerr << "usage: \\memo <text>\n"; continue; }
                    try {
                        icmg::imem::MemoryNode mn;
                        mn.topic = "chat-memo";
                        mn.content = txt;
                        mem.store(mn);
                        std::cerr << "(saved to project memory)\n";
                    } catch (const std::exception& e) {
                        std::cerr << "(memo save failed: " << e.what() << ")\n";
                    }
                    continue;
                }
                if (line.rfind("\\rule ", 0) == 0) {
                    std::string txt = line.substr(6);
                    if (txt.empty()) { std::cerr << "usage: \\rule <text>\n"; continue; }
                    std::string nm = "chat-" + sessionStamp();
                    std::string esc; esc.reserve(txt.size() + 8);
                    for (char c : txt) { if (c == '"') esc += "\\\""; else esc += c; }
                    std::string cmd = "icmg rule add / custom \"" + nm + "\" \"" + esc + "\"";
                    auto er = icmg::core::safeExecShell(cmd, true, 5000);
                    if (er.exit_code == 0) std::cerr << "(rule saved: " << nm << ")\n";
                    else std::cerr << "(rule save failed)\n";
                    continue;
                }
                if (line.rfind("\\unrule ", 0) == 0) {
                    std::string nm = line.substr(8);
                    if (nm.empty()) { std::cerr << "usage: \\unrule <rule-name>\n"; continue; }
                    std::string cmd = "icmg rule remove \"" + nm + "\"";
                    auto er = icmg::core::safeExecShell(cmd, true, 5000);
                    if (er.exit_code == 0) std::cerr << "(rule removed: " << nm << ")\n";
                    else std::cerr << "(rule remove failed)\n";
                    continue;
                }
                if (line == "\\rules") {
                    auto er = icmg::core::safeExecShell(
                        "icmg rule list --active 2>&1", true, 3000);
                    std::cerr << er.out << "\n";
                    continue;
                }
                if (line.rfind("\\skill ", 0) == 0) {
                    // \skill add <name> <path> | list | remove <name>
                    std::string rest = line.substr(7);
                    std::string cmd = "icmg skill " + rest + " 2>&1";
                    auto er = icmg::core::safeExecShell(cmd, true, 5000);
                    std::cerr << er.out;
                    if (er.exit_code != 0) std::cerr << "(skill cmd failed)\n";
                    continue;
                }
                std::cerr << "unknown command: " << line << "\n";
                continue;
            }

            // v1.50.0: auto-rule detection on plain user msg.
            // v1.53.0 Sub-D: interactive disambig on ambiguous fuzzy match.
            static auto _nl_adapters = buildNLAdapters();
            static std::vector<icmg::cli::FuzzyMatch> _pending_ambig;
            static icmg::cli::NLAction _pending_action = icmg::cli::NLAction::NONE;
            static std::string _pending_content;
            // 1) If pending disambig + line is "1".."9" -> resolve + execute.
            if (!_pending_ambig.empty() && line.size() <= 2 &&
                line.size() >= 1 && line[0] >= '1' && line[0] <= '9') {
                size_t idx = (size_t)(line[0] - '1');
                if (idx < _pending_ambig.size()) {
                    const auto& pick = _pending_ambig[idx];
                    std::string self = icmg::core::selfExePath();
                    std::string cmd;
                    if (_pending_action == icmg::cli::NLAction::REMOVE_RULE) {
                        cmd = "\"" + self + "\" rule disable " + pick.id;
                    } else if (_pending_action == icmg::cli::NLAction::EDIT_RULE) {
                        cmd = "\"" + self + "\" rule add / custom \"" + pick.name +
                              "\" \"" + _pending_content + "\" --update";
                    } else if (_pending_action == icmg::cli::NLAction::REMOVE_SKILL) {
                        cmd = "\"" + self + "\" skill remove " + pick.name;
                    }
                    if (!cmd.empty()) {
                        auto er = icmg::core::safeExecShell(cmd, true, 5000);
                        if (er.exit_code == 0) std::cerr << "(disambig pick #" << (idx+1)
                                << " executed: " << pick.name << ")\n";
                        else std::cerr << "(disambig exec failed)\n";
                    }
                    _pending_ambig.clear();
                    _pending_action = icmg::cli::NLAction::NONE;
                    _pending_content.clear();
                    continue;
                }
            }
            // 2) Any other input clears pending (non-numeric overrides stash).
            if (!_pending_ambig.empty()) {
                _pending_ambig.clear();
                _pending_action = icmg::cli::NLAction::NONE;
                _pending_content.clear();
            }
            auto _ack = icmg::cli::handleNL(line, _nl_adapters);
            if (!_ack.empty()) {
                std::cerr << _ack << "\n";
                // 3) Detect "ambiguous" ack -> stash candidates for next-turn pick.
                if (_ack.find("ambiguous") != std::string::npos) {
                    auto _detect = icmg::cli::detectNL(line);
                    if (_detect.action == icmg::cli::NLAction::REMOVE_RULE ||
                        _detect.action == icmg::cli::NLAction::EDIT_RULE) {
                        auto _corpus = _nl_adapters.rule_list ? _nl_adapters.rule_list()
                                                              : std::vector<icmg::cli::RuleRecord>{};
                        _pending_ambig = icmg::cli::listAmbiguous(_detect.target_name, _corpus);
                    } else if (_detect.action == icmg::cli::NLAction::REMOVE_SKILL) {
                        auto _corpus = _nl_adapters.skill_list ? _nl_adapters.skill_list()
                                                               : std::vector<icmg::cli::RuleRecord>{};
                        _pending_ambig = icmg::cli::listAmbiguous(_detect.target_name, _corpus);
                    }
                    if (!_pending_ambig.empty()) {
                        _pending_action = _detect.action;
                        _pending_content = _detect.content;
                        std::cerr << "  candidates:\n";
                        for (size_t i = 0; i < _pending_ambig.size(); ++i) {
                            std::cerr << "    " << (i+1) << ". " << _pending_ambig[i].name
                                      << " (score " << _pending_ambig[i].score << ")\n";
                        }
                        std::cerr << "  reply with 1.." << _pending_ambig.size()
                                  << " to pick, anything else cancels.\n";
                    }
                }
            }

            // v1.39.1 B: short-circuit to local warm-pool when --backend=local.
            if (local_backend && !no_llm) {
                // v1.52.0 EARLY: try cross-process warm-pipe BEFORE in-process WarmPool.
                // Daemon owns the model -> WarmPool::acquire would RAM-refuse here.
                // Build minimal prompt for the probe; uses ChatML wrap via warm daemon.
                // v1.53.0: capture daemon visibility BEFORE probe so that a pipe miss
                // (daemon visible but request failed) skips in-process acquire entirely.
                bool warm_was_available = icmg::llm::warmAvailable();
                if (warm_was_available) {
                    std::string sys_early = std::getenv("ICMG_NO_PERSONA")
                                              ? std::string{}
                                              : icmg::core::buildPersonaPrefix();
                    auto trimmed_early = icmg::llm::trimChatHistory(chat_history);
                    std::string prompt_early = icmg::llm::buildChatMLPromptMulti(
                        sys_early, trimmed_early, line);
                    icmg::llm::InferParams ip_early;
                    ip_early.max_tokens  = 8192;
                    ip_early.temperature = 0.7f;
                    ip_early.repeat_penalty = 1.25f;
                    ip_early.repeat_last_n = 128;
                    ip_early.presence_penalty = 0.4f;
                    ip_early.frequency_penalty = 0.3f;
                    ip_early.stop = icmg::llm::chatMLStopToken();
                    if (auto warm = icmg::llm::tryWarmInfer(prompt_early, ip_early,
                                        std::chrono::milliseconds(1000));
                        warm) {
                        chat_history.emplace_back("user", line);
                        chat_history.emplace_back("assistant", warm->text);
                        while (chat_history.size() > 6)  // v1.52.x: cap 20 -> 6 (3 user+ai pairs) - prevents pattern lock-in
                            chat_history.erase(chat_history.begin());
                        const auto& uid_w = icmg::core::currentUser();
                        icmg::llm::appendChatTurn(uid_w, session, "user", line,
                            "warm", 0, 0);
                        icmg::llm::appendChatTurn(uid_w, session, "assistant", warm->text,
                            "warm", warm->tok_in, warm->tok_out);
                        std::cout << warm->text << "\n";
                        continue;
                    }
                    // Daemon was visible but pipe request failed -- skip in-process acquire
                    // (it would RAM-refuse since daemon holds the model).
                    std::cerr << "(warm daemon visible but request failed - falling to agent)\n";
                    continue;
                }
                std::string err;
                auto* run = icmg::llm::WarmPool::instance().acquire(err);
                if (run) {
                    icmg::llm::InferParams ip;
                    ip.max_tokens  = 8192;  // v1.52.0: max headroom for long code/plan gen (n_ctx-bounded at runtime)
                    ip.temperature = 0.7f;
                    ip.repeat_penalty = 1.25f;
                    ip.repeat_last_n = 128;
                    ip.presence_penalty = 0.4f;
                    ip.frequency_penalty = 0.3f;
                    // v1.47.0: wrap prompt in ChatML so LLM treats role
                    // turns properly. Without this it autocompletes and
                    // fabricates User:/Assistant: lines, looping forever.
                    // Works for Qwen 2.5 family (default). Phi-3.5 and
                    // Llama-3.1 also accept ChatML via llama.cpp special-
                    // token parsing (graceful degrade).
                    // Opt-out: ICMG_NO_PERSONA=1 (persona) or
                    //          ICMG_NO_CHAT_TEMPLATE=1 (raw passthrough).
                    std::string sys = std::getenv("ICMG_NO_PERSONA")
                                          ? std::string{}
                                          : icmg::core::buildPersonaPrefix();
                    // v1.48.0 context injection (off via \context off).
                    if (inject_context) {
                        std::string ctx_block = buildContextInjection(
                            line, icmg::core::currentUser(), mem);
                        if (!ctx_block.empty()) {
                            sys += "\n" + ctx_block;
                            last_sources_dump = ctx_block;
                        } else {
                            last_sources_dump.clear();
                        }
                    }
                    // v1.48.0: persistent multi-turn history (function-scope
                    // state, mutable by slash cmds). Seeded once from DB.
                    if (!history_seeded) {
                        // v1.48.0 user wish: history cross-session, not
                        // bound to a single session_id. Loads most recent
                        // turns regardless of which session_id produced them.
                        // v1.52.0: default 0 turns (no cross-session seeding).
                        // Greeting bias from past sessions was confusing the model
                        // (replies opened "Halo, Cahyo!" instead of answering).
                        // Override via env ICMG_CHAT_SEED_TURNS=N.
                        int seed_turns = 0;
                        if (const char* e = std::getenv("ICMG_CHAT_SEED_TURNS"))
                            seed_turns = std::max(0, std::atoi(e));
                        if (seed_turns > 0) {
                            chat_history = icmg::llm::loadRecentTurns(
                                icmg::core::currentUser(), seed_turns);
                        }
                        history_seeded = true;
                    }
                    std::string prompt;
                    if (std::getenv("ICMG_NO_CHAT_TEMPLATE")) {
                        prompt = sys + line;
                    } else {
                        auto trimmed_history = icmg::llm::trimChatHistory(chat_history);
                        prompt  = icmg::llm::buildChatMLPromptMulti(sys, trimmed_history, line);
                        ip.stop = icmg::llm::chatMLStopToken();
                    }
                    // v1.52.0: try cross-process warm-pipe first.
                    if (auto warm = icmg::llm::tryWarmInfer(prompt, ip,
                                        std::chrono::milliseconds(1000));
                        warm) {
                        chat_history.emplace_back("user", line);
                        chat_history.emplace_back("assistant", warm->text);
                        while (chat_history.size() > 6)  // v1.52.x: cap 20 -> 6 (3 user+ai pairs) - prevents pattern lock-in
                            chat_history.erase(chat_history.begin());
                        const auto& uid2 = icmg::core::currentUser();
                        icmg::llm::appendChatTurn(uid2, session, "user", line,
                            "qwen2.5-7b-q4", 0, 0);
                        icmg::llm::appendChatTurn(uid2, session, "assistant", warm->text,
                            "qwen2.5-7b-q4", warm->tok_in, warm->tok_out);
                        std::cout << warm->text << "\n";
                        continue;
                    }
                    auto res = run->infer(prompt, ip);
                    if (res.ok) {
                        // Append turn to in-memory history (cap 20 entries).
                        chat_history.emplace_back("user", line);
                        chat_history.emplace_back("assistant", res.text);
                        while (chat_history.size() > 6) {
                            chat_history.erase(chat_history.begin());
                        }
                        // v1.48.0 B1: persist to local_llm_chats. Survives REPL close.
                        const auto& uid = icmg::core::currentUser();
                        icmg::llm::appendChatTurn(uid, session, "user", line,
                            "qwen2.5-7b-q4", res.tokens_in, 0);
                        icmg::llm::appendChatTurn(uid, session, "assistant", res.text,
                            "qwen2.5-7b-q4", 0, res.tokens_out);
                    }
                    if (res.ok) {
                        std::cout << res.text << "\n";
                        // Skip subprocess agent + skip auto-store
                        // (local replies in same memory store anyway).
                        continue;
                    }
                    std::cerr << "(local LLM failed: " << res.error
                              << ") fall through to agent\n";
                } else {
                    std::cerr << "(warm-pool acquire: " << err
                              << ") fall through to agent\n";
                }
            }

            // Build agent command — reuse `icmg agent` for prompt assembly + LLM call.
            std::string esc; esc.reserve(line.size() * 2);
            for (char c : line) {
                if (c == '"' || c == '\\') esc.push_back('\\');
                esc.push_back(c);
            }
            std::string cmd = "\"" + self_path + "\" agent \"" + esc + "\"";
            if (no_pack) cmd += " --no-pack";
            if (no_llm)  cmd += " --dry-run";
            // Tag stored decision with chat session.
            // (icmg agent always stores; we let it; topic prefix via env not supported here.)

            auto res = core::safeExecShell(cmd, false, 120000);
            if (res.exit_code != 0) {
                std::cerr << "(agent failed: exit " << res.exit_code << ")\n";
                if (!res.err.empty()) std::cerr << res.err << "\n";
                continue;
            }
            std::cout << res.out;
            if (res.out.empty() || res.out.back() != '\n') std::cout << "\n";

            // Auto-store as chat memory.
            try {
                imem::MemoryNode n;
                n.topic   = "chat-" + session + " " + (line.size() > 60 ? line.substr(0, 60) + "..." : line);
                n.content = res.out.substr(0, 4000);
                n.keywords = "chat,session-" + session;
                n.importance = 1;
                mem.store(n, true);
            } catch (...) {}
        }

        return 0;
    }

private:
    std::string locateSelf() {
#ifdef _WIN32
        char buf[1024]; GetModuleFileNameA(nullptr, buf, sizeof(buf));
        return buf;
#else
        return "icmg";
#endif
    }

    void saveSession(core::Db& db, const std::string& name, const std::string& session) {
        // Use existing sessions table from Phase 19 if present; else fall back.
        try {
            db.run("CREATE TABLE IF NOT EXISTS sessions("
                   "name TEXT PRIMARY KEY,"
                   "snapshot TEXT NOT NULL,"
                   "created_at INTEGER NOT NULL DEFAULT (strftime('%s','now')))");
            std::string snap = "{\"session\":\"" + session + "\"}";
            db.run("INSERT OR REPLACE INTO sessions(name,snapshot) VALUES(?,?)",
                   {name, snap});
        } catch (...) {}
    }

    std::string loadSession(core::Db& db, const std::string& name) {
        std::string out;
        try {
            db.query("SELECT snapshot FROM sessions WHERE name=?",
                     {name},
                     [&](const core::Row& r) { if (!r.empty()) out = r[0]; });
        } catch (...) {}
        return out;
    }
};

ICMG_REGISTER_COMMAND("chat", ChatCommand);

} // namespace icmg::cli

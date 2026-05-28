// v1.41.0 `icmg persona` — per-user persona storage in global DB.
// Multi-user single-server: each user_id keeps own persona+traits.
// Used by chat/agent/ask as system-prompt prefix (consumer wires).
//
// Sub-cmds:
//   set <name> [--traits "<free-text>"]   upsert current user's persona
//   show [--user <id>]                    print current (or named) user's persona
//   list                                  list all users + persona names (admin view)
//   clear                                 remove current user's persona row
//
// Storage only. Model still enforces own content policies regardless of
// what's stored here — db is data, not authorization.
// v1.41.0 NOTE: `import std;` Modules pilot was ATTEMPTED and BLOCKED here.
// MSYS2 mingw-w64-gcc 15.2 does NOT ship libstdc++.modules.json yet, so
// CMake's experimental Modules feature refuses to enable on this platform
// even with the UUID opt-in key. Real Modules adoption is a platform
// dependency, not a code-readiness issue. Tracked as known-issue; revisit
// when MSYS2 packages modules.json. Falls back to classic #include here.
#include <format>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/global_db.hpp"
#include "../../core/persona_db.hpp"   // v1.57 S2: exe-dir persona DB
#include "../../core/user_identity.hpp"
#include "../../core/result.hpp"  // v1.41.0 std::expected adoption

namespace icmg::cli {

class PersonaCommand : public BaseCommand {
public:
    std::string name() const override { return "persona"; }
    std::string description() const override {
        return "Per-user persona (set/show/list/clear)";
    }
    void usage() const override {
        std::cout <<
            "Usage: icmg persona <subcommand> [args]\n\n"
            "Subcommands:\n"
            "  set <name> [--traits \"...\"]   Upsert current user's persona\n"
            "  show [--user <id>]            Show persona (default: current user)\n"
            "  list                          List all users + persona names\n"
            "  clear                         Remove current user's persona\n"
            "  context [--json]              Emit hook prefix (for UserPromptSubmit)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help") { usage(); return 0; }
        const std::string& sub = args[0];

        auto& gdb = core::GlobalDb::instance();
        gdb.init();  // ensure migrations applied (0031 user_personas)
        const std::string user = core::currentUser();

        if (sub == "set") {
            if (args.size() < 2) {
                std::cerr << "icmg persona set: <name> required\n";
                return 1;
            }
            std::string persona = args[1];
            std::string traits = flagValue(args, "--traits", "");
            auto r = upsertPersona(gdb, user, persona, traits);
            if (!r) {
                std::cerr << std::format("icmg persona set: {}\n", r.error().msg);
                return r.error().code;
            }
            std::cout << std::format("persona set: user={} name={}{}\n",
                                     user, persona,
                                     traits.empty() ? "" : " (with traits)");
            return 0;
        }

        if (sub == "show") {
            std::string target = flagValue(args, "--user", user);
            auto r = fetchPersona(gdb, target);
            if (!r) {
                std::cerr << std::format("icmg persona show: {}\n", r.error().msg);
                return r.error().code;
            }
            const auto& [persona, traits] = r.value();
            if (persona.empty()) {
                std::cout << std::format("(no persona for user: {})\n", target);
                return 0;
            }
            std::cout << std::format("user:    {}\npersona: {}\ntraits:  {}\n",
                                     target, persona, traits);
            return 0;
        }

        if (sub == "list") {
            try {
                gdb.db().query("SELECT user_id, persona, length(traits) FROM user_personas "
                          "ORDER BY updated_at DESC",
                          {},
                          [&](const core::Row& r) {
                              if (r.size() < 3) return;
                              std::cout << std::format("  {:<20} {:<24} traits={}B\n",
                                                       r[0], r[1], r[2]);
                          });
            } catch (const std::exception& e) {
                std::cerr << std::format("icmg persona list: {}\n", e.what());
                return 1;
            }
            return 0;
        }

        if (sub == "clear") {
            auto r = clearPersona(gdb, user);
            if (!r) {
                std::cerr << std::format("icmg persona clear: {}\n", r.error().msg);
                return r.error().code;
            }
            std::cout << std::format("persona cleared: user={}\n", user);
            return 0;
        }

        // v1.43.0 Phase 1: hook output for UserPromptSubmit injection.
        // Emits Claude-Code JSON {hookSpecificOutput:{additionalContext:"..."}}
        // when persona set, else empty JSON {} so hook is no-op.
        if (sub == "context") {
            bool as_json = hasFlag(args, "--json");
            auto r = fetchPersona(gdb, user);
            if (!r || r.value().first.empty()) {
                if (as_json) std::cout << "{}\n";
                return 0;
            }
            const auto& [persona, traits] = r.value();
            std::string ctx = traits.empty()
                ? std::format("[Persona: {}]\n\n", persona)
                : std::format("[Persona: {} | Traits: {}]\n\n", persona, traits);
            if (as_json) {
                // Escape ctx for JSON
                std::string esc;
                esc.reserve(ctx.size() * 2);
                for (char c : ctx) {
                    switch (c) {
                        case '"':  esc += "\\\""; break;
                        case '\\': esc += "\\\\"; break;
                        case '\n': esc += "\\n";  break;
                        case '\r': esc += "\\r";  break;
                        case '\t': esc += "\\t";  break;
                        default:   esc += c;
                    }
                }
                std::cout << std::format(
                    "{{\"hookSpecificOutput\":{{\"hookEventName\":\"UserPromptSubmit\","
                    "\"additionalContext\":\"{}\"}}}}\n",
                    esc);
            } else {
                std::cout << ctx;
            }
            return 0;
        }

        std::cerr << std::format("persona: unknown subcommand '{}'\n", sub);
        return 1;
    }

private:
    // v1.41.0 std::expected adoption — Result<int> = rows-affected on success.
    static core::Result<int> upsertPersona(core::GlobalDb& /*gdb*/,
                                           const std::string& user,
                                           const std::string& persona,
                                           const std::string& traits) {
        // v1.57 S2: write to exe-dir persona DB (falls back to global DB
        // inside writePersona when exe dir is not writable).
        if (core::writePersona(user, persona, traits)) return 1;
        return core::err(2, "persona write failed (exe-dir + global both)");
    }

    static core::Result<std::pair<std::string, std::string>>
    fetchPersona(core::GlobalDb& /*gdb*/, const std::string& user) {
        std::pair<std::string, std::string> out;
        // readPersona tries exe-dir first, then legacy global DB.
        core::readPersona(user, out.first, out.second);
        return out;
    }

    static core::Result<int> clearPersona(core::GlobalDb& gdb,
                                           const std::string& user) {
        // Delete from BOTH stores so a clear is total regardless of where
        // the row lived (exe-dir for v1.57+, global for legacy).
        try {
            if (core::personaDbAvailable())
                core::personaDb().run(
                    "DELETE FROM user_personas WHERE user_id=?", {user});
        } catch (...) { /* non-fatal */ }
        try {
            gdb.db().run("DELETE FROM user_personas WHERE user_id=?", {user});
            return 1;
        } catch (const std::exception& e) {
            return core::err(2, e.what());
        }
    }
};

ICMG_REGISTER_COMMAND("persona", PersonaCommand);

}  // namespace icmg::cli

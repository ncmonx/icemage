// Phase 75: `icmg repair-history` — surface audit log + counter state.
//
// Subcommands:
//   tail [--limit N]     Last N audit entries (default 20)
//   verify               HMAC chain check; non-zero exit if tamper detected
//   count                Repair attempts in last hour (per-kind breakdown)
//   reset-counter        Clear loop guard (use after manual investigation)

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/audit_log.hpp"
#include "../../core/repair_counter.hpp"
#include "../../core/path_utils.hpp"

#include <filesystem>
#include <iostream>
#include <map>
#include <string>

namespace fs = std::filesystem;

namespace icmg::cli {

class RepairHistoryCommand : public BaseCommand {
public:
    std::string name()        const override { return "repair-history"; }
    std::string description() const override {
        return "Audit log + repair-loop counter (tail/verify/count/reset)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg repair-history <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  tail [--limit N]   Last N audit entries (default 20)\n"
            "  verify             HMAC chain integrity check\n"
            "  count              Repair attempts last hour (per-kind)\n"
            "  reset-counter      Clear loop guard (after manual fix)\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || hasFlag(args, "--help")) { usage(); return 0; }
        const std::string& sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());
        if (sub == "tail")          return cmdTail(rest);
        if (sub == "verify")        return cmdVerify(rest);
        if (sub == "count")         return cmdCount(rest);
        if (sub == "reset-counter") return cmdReset(rest);
        std::cerr << "icmg repair-history: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }

private:
    static fs::path projectAuditPath() {
        return fs::current_path() / ".icmg" / "audit.log";
    }

    int cmdTail(const std::vector<std::string>& args) {
        int limit = 20;
        try { limit = std::stoi(flagValue(args, "--limit", "20")); } catch (...) {}
        if (!fs::exists(projectAuditPath())) {
            std::cout << "  (no audit log yet — no repair events recorded)\n";
            return 0;
        }
        core::AuditLog al(projectAuditPath().string());
        auto entries = al.tail(limit);
        if (entries.empty()) { std::cout << "  (empty)\n"; return 0; }
        std::cout << "TS                    ACTOR     EVENT       PAYLOAD\n";
        std::cout << std::string(80, '-') << "\n";
        for (auto& e : entries) {
            std::cout << e.ts << "  "
                      << (e.actor.size() > 8 ? e.actor.substr(0, 8) : e.actor)
                      << std::string(std::max(0, 8 - (int)e.actor.size() + 2), ' ')
                      << e.event;
            for (int i = (int)e.event.size(); i < 12; ++i) std::cout << ' ';
            std::cout << e.payload << "\n";
        }
        return 0;
    }

    int cmdVerify(const std::vector<std::string>&) {
        if (!fs::exists(projectAuditPath())) {
            std::cout << "  (no audit log to verify)\n";
            return 0;
        }
        core::AuditLog al(projectAuditPath().string());
        std::vector<int64_t> bad;
        int n = al.verify(&bad);
        if (n == 0) {
            std::cout << "[OK] audit log chain intact (" << al.read().size()
                      << " entries)\n";
            return 0;
        }
        std::cout << "[!!] audit log chain BROKEN — " << n << " bad row(s):\n";
        for (auto ln : bad) std::cout << "  line " << ln << "\n";
        std::cout << "Possible causes: manual edit, partial write, key file changed.\n";
        return 2;
    }

    int cmdCount(const std::vector<std::string>&) {
        core::RepairCounter rc;
        auto events = rc.recent(3600);
        std::map<std::string, int> by_kind;
        for (auto& e : events) by_kind[e.kind]++;
        std::cout << "Repair attempts (last hour): " << events.size() << "\n";
        if (events.empty()) { std::cout << "  (none — system stable)\n"; return 0; }
        for (auto& [k, n] : by_kind) {
            std::cout << "  " << k;
            for (int i = (int)k.size(); i < 24; ++i) std::cout << ' ';
            std::cout << n << "\n";
        }
        if (events.size() >= 3)
            std::cout << "WARN: " << events.size() << " repairs in 1h — investigate.\n";
        return 0;
    }

    int cmdReset(const std::vector<std::string>&) {
        core::RepairCounter rc;
        rc.reset();
        std::cout << "icmg repair-history: counter cleared.\n";
        return 0;
    }
};

ICMG_REGISTER_COMMAND("repair-history", RepairHistoryCommand);

} // namespace icmg::cli

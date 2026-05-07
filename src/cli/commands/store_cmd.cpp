#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"
#include "../../imem/memory_store.hpp"
#include "../../imem/memory_node.hpp"
#include <iostream>
#include <string>
#include <chrono>

namespace icmg::cli {

class StoreCommand : public BaseCommand {
public:
    std::string name()        const override { return "store"; }
    std::string description() const override { return "Store a memory node"; }

    void usage() const override {
        std::cout <<
            "Usage: icmg store <topic> <content> [options]\n\n"
            "Options:\n"
            "  --importance low|med|high|crit  Importance level (default: med)\n"
            "  --kw k1,k2,...                  Comma-separated keywords\n"
            "  --ttl <days>                    Expire after N days\n"
            "  --force                         Store even if duplicate detected\n"
            "  --json                          JSON output\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (args.empty() || args[0] == "--help" || args[0] == "-h") {
            usage(); return 0;
        }
        if (args.size() < 2) {
            std::cerr << "icmg store: requires <topic> <content>\n";
            return 1;
        }

        std::string topic   = args[0];
        std::string content = args[1];
        std::string kw      = flagValue(args, "--kw");
        std::string imp_str = flagValue(args, "--importance", "med");
        std::string ttl_str = flagValue(args, "--ttl");
        bool force          = hasFlag(args, "--force");
        bool json_out       = hasFlag(args, "--json");

        imem::MemoryNode node;
        node.topic      = topic;
        node.content    = content;
        node.keywords   = kw;
        node.importance = imem::importanceFromName(imp_str);

        if (!ttl_str.empty()) {
            try {
                int days = std::stoi(ttl_str);
                int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count();
                node.expires_at = now + (int64_t)days * 86400;
            } catch (...) {}
        }

        auto& cfg = core::Config::instance();
        core::Db db(cfg.projectDbPath("."));
        imem::MemoryStore store(db);

        try {
            int64_t id = store.store(node, force);
            if (json_out) {
                std::cout << "{\"id\":" << id
                          << ",\"topic\":\"" << topic << "\""
                          << ",\"status\":\"stored\"}\n";
            } else {
                std::cout << "Stored [#" << id << "] " << topic << "\n";
            }
            return 0;
        } catch (const imem::DuplicateError& e) {
            if (json_out) {
                std::cout << "{\"error\":\"duplicate\",\"existing_id\":"
                          << e.existing_id << ",\"message\":\""
                          << e.what() << "\"}\n";
            } else {
                std::cerr << "icmg store: " << e.what() << "\n";
            }
            return 1;
        } catch (const std::exception& e) {
            std::cerr << "icmg store: " << e.what() << "\n";
            return 1;
        }
    }
};

ICMG_REGISTER_COMMAND("store", StoreCommand);

} // namespace icmg::cli

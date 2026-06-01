// v1.21.1 (FB1): user-facing feedback loop CLI.
//
// `icmg feedback record --topic X --predicted "Y" --actual "Z" [--note ...]`
// `icmg feedback search "<query>"`            — substring match across topic+predicted+actual
// `icmg feedback stats`                       — count by topic + most-applied
// `icmg feedback apply <id>`                  — bumps applied_count for an entry
//
// Backed by `feedbacks` table (schema mig 0033).

#include "../base_command.hpp"
#include "../../core/registry.hpp"
#include "../../core/config.hpp"
#include "../../core/db.hpp"

#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace icmg::cli {

class FeedbackLoopCommand : public BaseCommand {
public:
    std::string name()        const override { return "feedback-loop"; }
    std::string description() const override {
        return "User-facing feedback loop: record/search/stats/apply predictions (v1.21.1 FB1)";
    }

    void usage() const override {
        std::cout <<
            "Usage: icmg feedback-loop <subcommand> [options]\n\n"
            "Subcommands:\n"
            "  record --topic X --predicted \"Y\" --actual \"Z\" [--note ...]\n"
            "    Record a correction. Predicted = what the AI said; actual = what was right.\n"
            "  search \"<query>\"\n"
            "    Substring scan across topic + predicted + actual. Use BEFORE similar prediction.\n"
            "  stats\n"
            "    Counts by topic + most-applied entries.\n"
            "  apply <id>\n"
            "    Bump applied_count for an entry (lets stats reflect real usefulness).\n";
    }

    int run(const std::vector<std::string>& args) override {
        if (hasFlag(args, "--help") || hasFlag(args, "-h") || args.empty()) {
            usage(); return 0;
        }
        std::string sub = args[0];
        std::vector<std::string> rest(args.begin() + 1, args.end());

        core::Db db(core::Config::instance().projectDbPath("."));

        if (sub == "record") {
            std::string topic = flagValue(rest, "--topic");
            std::string predicted = flagValue(rest, "--predicted");
            std::string actual = flagValue(rest, "--actual");
            std::string note = flagValue(rest, "--note");
            if (topic.empty() || predicted.empty() || actual.empty()) {
                std::cerr << "feedback record: --topic, --predicted, --actual all required\n";
                return 1;
            }
            db.run("INSERT INTO feedbacks(topic, predicted, actual, note) VALUES(?,?,?,?)",
                   {topic, predicted, actual, note});
            std::cout << "feedback recorded under topic '" << topic << "'\n";
            return 0;
        }

        if (sub == "search") {
            if (rest.empty()) { std::cerr << "feedback search: <query> required\n"; return 1; }
            std::string q = rest[0];
            std::string like = "%" + q + "%";
            int found = 0;
            db.query(
                "SELECT id, topic, predicted, actual, note, applied_count, created_at "
                "FROM feedbacks "
                "WHERE topic LIKE ? OR predicted LIKE ? OR actual LIKE ? "
                "ORDER BY applied_count DESC, created_at DESC LIMIT 20",
                {like, like, like},
                [&](const core::Row& r) {
                    if (r.size() < 7) return;
                    ++found;
                    std::cout << "#" << r[0] << "  [" << r[1] << "]  applied=" << r[5] << "\n";
                    std::cout << "  predicted: " << r[2] << "\n";
                    std::cout << "  actual:    " << r[3] << "\n";
                    if (!r[4].empty()) std::cout << "  note:      " << r[4] << "\n";
                    std::cout << "\n";
                });
            if (found == 0) std::cout << "feedback search: 0 matches for \"" << q << "\"\n";
            return 0;
        }

        if (sub == "stats") {
            std::map<std::string, int> by_topic;
            int total = 0;
            db.query("SELECT topic, COUNT(*) FROM feedbacks GROUP BY topic", {},
                     [&](const core::Row& r){
                         if (r.size() < 2) return;
                         try { by_topic[r[0]] = std::stoi(r[1]); total += std::stoi(r[1]); }
                         catch (...) {}
                     });
            std::cout << "=== Feedback stats ===\nTotal: " << total << "\n\n";
            std::vector<std::pair<std::string,int>> sorted(by_topic.begin(), by_topic.end());
            std::sort(sorted.begin(), sorted.end(),
                      [](auto&a, auto&b){ return a.second > b.second; });
            std::cout << "By topic (top 10):\n";
            for (size_t i = 0; i < sorted.size() && i < 10; ++i) {
                std::cout << "  " << std::left << std::setw(28) << sorted[i].first
                          << sorted[i].second << "\n";
            }
            std::cout << "\nMost-applied (top 5):\n";
            db.query("SELECT id, topic, predicted, actual, applied_count "
                     "FROM feedbacks ORDER BY applied_count DESC LIMIT 5", {},
                     [&](const core::Row& r){
                         if (r.size() < 5) return;
                         std::cout << "  #" << r[0] << "  [" << r[1] << "]  applied="
                                   << r[4] << "\n    " << r[2].substr(0, 60) << " -> "
                                   << r[3].substr(0, 60) << "\n";
                     });
            return 0;
        }

        if (sub == "apply") {
            if (rest.empty()) { std::cerr << "feedback apply: <id> required\n"; return 1; }
            db.run("UPDATE feedbacks SET applied_count = applied_count + 1 WHERE id = ?",
                   {rest[0]});
            std::cout << "feedback #" << rest[0] << " applied_count bumped\n";
            return 0;
        }

        std::cerr << "feedback: unknown subcommand '" << sub << "'\n";
        usage();
        return 1;
    }
};

ICMG_REGISTER_COMMAND("feedback-loop", FeedbackLoopCommand);

} // namespace icmg::cli

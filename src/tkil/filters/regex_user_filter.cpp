// v1.21.1 (F2): per-project filter loader.
// Reads `.icmg/filters.toml` (minimal subset of TOML — `[[filter]]` arrays
// with `match` + `strip` lists) on tkil dispatch and applies user-supplied
// regex stripping on top of the built-in filter pipeline.
//
// Schema example:
//   [[filter]]
//   match = "^cargo (test|build)"
//   strip = ["^[[:space:]]*Compiling ", "^[[:space:]]*Finished "]
//
//   [[filter]]
//   match = "^docker logs"
//   strip = ["^[[:space:]]*$"]
//
// Loader is a singleton; reload on `.icmg/filters.toml` mtime change.

#include "filter_utils.hpp"
#include "../../core/registry.hpp"
#include "../../core/path_utils.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>
#include <vector>

namespace icmg::tkil {
namespace {

struct UserFilter {
    std::regex matcher;
    std::vector<std::regex> strippers;
};

class UserFilterStore {
public:
    static UserFilterStore& instance() {
        static UserFilterStore s;
        return s;
    }

    std::vector<UserFilter> load() {
        std::lock_guard<std::mutex> lk(mu_);
        auto path = filtersPath();
        std::error_code ec;
        auto mtime = std::filesystem::exists(path, ec)
                         ? std::filesystem::last_write_time(path, ec)
                               .time_since_epoch().count()
                         : 0;
        if (mtime == last_mtime_) return filters_;
        last_mtime_ = mtime;
        filters_.clear();
        if (mtime == 0) return filters_;
        parseFile(path);
        return filters_;
    }

private:
    std::filesystem::path filtersPath() const {
        return std::filesystem::current_path() / ".icmg" / "filters.toml";
    }

    void parseFile(const std::filesystem::path& p) {
        std::ifstream f(p);
        if (!f) return;
        std::string line;
        UserFilter current;
        bool have_match = false;
        bool in_block = false;
        while (std::getline(f, line)) {
            // Strip comments + trim.
            auto hash = line.find('#');
            if (hash != std::string::npos) line.erase(hash);
            size_t s = line.find_first_not_of(" \t");
            size_t e = line.find_last_not_of(" \t\r\n");
            if (s == std::string::npos) { continue; }
            line = line.substr(s, e - s + 1);
            if (line.empty()) continue;
            if (line == "[[filter]]") {
                if (in_block && have_match) filters_.push_back(std::move(current));
                current = UserFilter{};
                have_match = false;
                in_block = true;
                continue;
            }
            if (!in_block) continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            // trim
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            size_t vs = val.find_first_not_of(" \t");
            if (vs != std::string::npos) val = val.substr(vs);
            if (key == "match") {
                std::string m = stripQuotes(val);
                try { current.matcher = std::regex(m); have_match = true; }
                catch (...) {}
            } else if (key == "strip") {
                // Either "literal-string" or [ "a", "b", "c" ]
                if (!val.empty() && val.front() == '[') {
                    // Array form. Naive: extract quoted items.
                    std::regex item_re("\"([^\"]*)\"");
                    auto begin = std::sregex_iterator(val.begin(), val.end(), item_re);
                    auto end   = std::sregex_iterator();
                    for (auto it = begin; it != end; ++it) {
                        try { current.strippers.emplace_back((*it)[1].str()); }
                        catch (...) {}
                    }
                } else {
                    std::string m = stripQuotes(val);
                    try { current.strippers.emplace_back(m); } catch (...) {}
                }
            }
        }
        if (in_block && have_match) filters_.push_back(std::move(current));
    }

    static std::string stripQuotes(const std::string& s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"')
            return s.substr(1, s.size() - 2);
        return s;
    }

    std::mutex mu_;
    long long last_mtime_ = 0;
    std::vector<UserFilter> filters_;
};

} // namespace

// Stand-alone helper: apply user-toml filters AFTER built-in filter has run.
// Returns input unchanged if no `.icmg/filters.toml` present.
std::string applyUserFilters(const std::string& filtered,
                             const std::string& command) {
    auto rules = UserFilterStore::instance().load();
    if (rules.empty()) return filtered;
    // First matching rule wins (avoids cascading strips from multiple matchers).
    for (auto& rule : rules) {
        if (!std::regex_search(command, rule.matcher)) continue;
        std::stringstream in(filtered), out;
        std::string line;
        while (std::getline(in, line)) {
            bool drop = false;
            for (auto& str : rule.strippers) {
                if (std::regex_search(line, str)) { drop = true; break; }
            }
            if (!drop) out << line << "\n";
        }
        return out.str();
    }
    return filtered;
}

} // namespace icmg::tkil

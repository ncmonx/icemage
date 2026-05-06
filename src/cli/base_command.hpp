#pragma once
#include <string>
#include <vector>
#include <iostream>

namespace icmg::cli {

class BaseCommand {
public:
    virtual ~BaseCommand() = default;
    virtual std::string name()        const = 0;
    virtual std::string description() const = 0;
    virtual int run(const std::vector<std::string>& args) = 0;

    // Print usage to stdout
    virtual void usage() const {
        std::cout << "Usage: icmg " << name() << " [options]\n"
                  << "  " << description() << "\n";
    }

protected:
    // True if args contains flag
    static bool hasFlag(const std::vector<std::string>& args, const std::string& flag) {
        for (auto& a : args) if (a == flag) return true;
        return false;
    }

    // Get value after --key=value or --key value
    static std::string flagValue(const std::vector<std::string>& args,
                                 const std::string& key,
                                 const std::string& def = "") {
        for (size_t i = 0; i < args.size(); ++i) {
            if (args[i].substr(0, key.size() + 1) == key + "=")
                return args[i].substr(key.size() + 1);
            if (args[i] == key && i + 1 < args.size())
                return args[i + 1];
        }
        return def;
    }
};

} // namespace icmg::cli

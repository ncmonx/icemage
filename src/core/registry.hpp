#pragma once
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <stdexcept>

namespace icmg::core {

// Generic factory registry: maps string key → factory function.
// Thread-safe for registration (happens at static init, single-threaded).
// Thread-safe for lookup (read-only after init).
template<typename Base>
class Registry {
public:
    using Factory = std::function<std::unique_ptr<Base>()>;

    static Registry& instance() {
        static Registry inst;
        return inst;
    }

    void reg(const std::string& key, Factory f) {
        map_[key] = std::move(f);
    }

    bool has(const std::string& key) const {
        return map_.count(key) > 0;
    }

    std::unique_ptr<Base> create(const std::string& key) const {
        auto it = map_.find(key);
        if (it == map_.end())
            throw std::runtime_error("Registry: unknown key: " + key);
        return it->second();
    }

    std::vector<std::string> keys() const {
        std::vector<std::string> ks;
        ks.reserve(map_.size());
        for (auto& [k, _] : map_) ks.push_back(k);
        return ks;
    }

private:
    std::unordered_map<std::string, Factory> map_;
};

} // namespace icmg::core

// ---- Forward declarations for real base types (defined in their own headers) ----
namespace icmg::graph { class BaseExtractor; }
namespace icmg::cli   { class BaseCommand;   }
namespace icmg::rtk   { class BaseFilter;    }
namespace icmg {
    class  BaseImporter;       // defined in src/import/base_importer.hpp
    struct BaseMcpTool    {};  // defined in phase-13
}

// ---- Registration macros ----

// ICMG_REGISTER_EXTRACTOR("cpp", MyCppExtractor)
#define ICMG_REGISTER_EXTRACTOR(lang, Class) \
    static bool _reg_ext_##Class = []() { \
        ::icmg::core::Registry<::icmg::graph::BaseExtractor>::instance() \
            .reg(lang, []() { return std::make_unique<Class>(); }); \
        return true; \
    }()

// ICMG_REGISTER_FILTER("git", GitFilter)
#define ICMG_REGISTER_FILTER(pattern, Class) \
    static bool _reg_flt_##Class = []() { \
        ::icmg::core::Registry<::icmg::rtk::BaseFilter>::instance() \
            .reg(pattern, []() { return std::make_unique<Class>(); }); \
        return true; \
    }()

// ICMG_REGISTER_IMPORTER("icm", IcmImporter)
#define ICMG_REGISTER_IMPORTER(name, Class) \
    static bool _reg_imp_##Class = []() { \
        ::icmg::core::Registry<::icmg::BaseImporter>::instance() \
            .reg(name, []() { return std::make_unique<Class>(); }); \
        return true; \
    }()

// ICMG_REGISTER_COMMAND("store", StoreCommand)
#define ICMG_REGISTER_COMMAND(name, Class) \
    static bool _reg_cmd_##Class = []() { \
        ::icmg::core::Registry<::icmg::cli::BaseCommand>::instance() \
            .reg(name, []() { return std::make_unique<Class>(); }); \
        return true; \
    }()

// ICMG_REGISTER_MCP_TOOL("memory_store", MemoryStoreTool)
#define ICMG_REGISTER_MCP_TOOL(name, Class) \
    static bool _reg_mcp_##Class = []() { \
        ::icmg::core::Registry<::icmg::BaseMcpTool>::instance() \
            .reg(name, []() { return std::make_unique<Class>(); }); \
        return true; \
    }()

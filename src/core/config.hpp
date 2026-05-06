#pragma once
#include <string>
#include <unordered_map>

namespace icmg::core {

class Config {
public:
    static Config& instance();

    // Load from default path (~/.icmg/config.json or %APPDATA%\icmg\config.json).
    void load();
    // Load from explicit path.
    void load(const std::string& path);
    void save() const;

    bool        getBool  (const std::string& key, bool def = false)        const;
    int         getInt   (const std::string& key, int def = 0)             const;
    std::string getString(const std::string& key, const std::string& def = "") const;
    void        set      (const std::string& key, const std::string& value);

    // Convenience accessors
    bool        verbose()    const { return verbose_; }
    void        setVerbose(bool v) { verbose_ = v; }
    std::string globalDbPath()                     const;
    std::string projectDbPath(const std::string& root) const;
    std::string logPath()    const;

    void log(const std::string& msg) const; // print if verbose

private:
    Config() = default;
    std::unordered_map<std::string, std::string> data_;
    std::string path_;
    bool verbose_ = false;
    int  config_version_ = 1;

    void parseJson(const std::string& json);
    std::string toJson() const;
};

} // namespace icmg::core

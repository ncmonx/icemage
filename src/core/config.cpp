#include "config.hpp"
#include "path_utils.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include <algorithm>
#include <stdexcept>

namespace fs = std::filesystem;
namespace icmg::core {

Config& Config::instance() {
    static Config inst;
    return inst;
}

void Config::load() {
    std::string dir = icmgGlobalDir();
    path_ = dir + "/config.json";
    if (fs::exists(path_)) {
        load(path_);
    } else {
        // Write defaults
        save();
    }
}

void Config::load(const std::string& path) {
    path_ = path;
    std::ifstream f(path);
    if (!f) return;
    std::string json((std::istreambuf_iterator<char>(f)),
                      std::istreambuf_iterator<char>());
    parseJson(json);

    // Validate version
    auto it = data_.find("version");
    if (it == data_.end()) {
        if (verbose_) std::cerr << "[icmg] config: no version field, assuming v1\n";
    } else {
        try { config_version_ = std::stoi(it->second); }
        catch (...) {}
    }

    verbose_ = getBool("verbose", false);
}

void Config::save() const {
    std::string dir = fs::path(path_).parent_path().string();
    if (!dir.empty()) fs::create_directories(dir);

    std::ofstream f(path_);
    if (!f) return;
    f << toJson();
}

bool Config::getBool(const std::string& key, bool def) const {
    auto it = data_.find(key);
    if (it == data_.end()) return def;
    std::string v = it->second;
    std::transform(v.begin(), v.end(), v.begin(), ::tolower);
    return (v == "true" || v == "1" || v == "yes");
}

int Config::getInt(const std::string& key, int def) const {
    auto it = data_.find(key);
    if (it == data_.end()) return def;
    try { return std::stoi(it->second); }
    catch (...) { return def; }
}

std::string Config::getString(const std::string& key, const std::string& def) const {
    auto it = data_.find(key);
    return it == data_.end() ? def : it->second;
}

void Config::set(const std::string& key, const std::string& value) {
    data_[key] = value;
}

std::string Config::globalDbPath() const {
    std::string dir = getString("global_dir", icmgGlobalDir());
    return dir + "/global.db";
}

std::string Config::projectDbPath(const std::string& root) const {
    if (!project_db_override_.empty()) return project_db_override_;
    fs::path r = fs::weakly_canonical(root);
    return (r / ".icmg" / "data.db").string();
}

std::string Config::logPath() const {
    return icmgGlobalDir() + "/icmg.log";
}

void Config::log(const std::string& msg) const {
    if (verbose_) std::cerr << "[icmg] " << msg << "\n";
}

// ---- Minimal flat-key JSON parser ----
// Format: { "key": "value", "key2": true/false/number }
// No nesting support needed for config.

void Config::parseJson(const std::string& json) {
    size_t i = 0;
    auto skip_ws = [&]() {
        while (i < json.size() && std::isspace((unsigned char)json[i])) ++i;
    };
    auto read_string = [&]() -> std::string {
        if (i >= json.size() || json[i] != '"') return "";
        ++i;
        std::string s;
        while (i < json.size() && json[i] != '"') {
            if (json[i] == '\\' && i + 1 < json.size()) { ++i; s += json[i]; }
            else s += json[i];
            ++i;
        }
        if (i < json.size()) ++i; // closing "
        return s;
    };

    skip_ws();
    if (i >= json.size() || json[i] != '{') return;
    ++i;

    while (i < json.size()) {
        skip_ws();
        if (json[i] == '}') break;
        if (json[i] == ',') { ++i; continue; }

        // key
        skip_ws();
        std::string key = read_string();
        skip_ws();
        if (i >= json.size() || json[i] != ':') break;
        ++i;
        skip_ws();

        // value
        std::string value;
        if (json[i] == '"') {
            value = read_string();
        } else {
            // number/bool/null — read until , or }
            size_t start = i;
            while (i < json.size() && json[i] != ',' && json[i] != '}') ++i;
            value = json.substr(start, i - start);
            // trim trailing whitespace
            while (!value.empty() && std::isspace((unsigned char)value.back()))
                value.pop_back();
        }

        if (!key.empty()) data_[key] = value;
    }
}

std::string Config::toJson() const {
    std::ostringstream o;
    o << "{\n";
    o << "  \"version\": " << config_version_ << ",\n";
    bool first = true;
    for (auto& [k, v] : data_) {
        if (k == "version") continue;
        if (!first) o << ",\n";
        first = false;
        // Detect non-string values (booleans, numbers)
        bool is_str = true;
        if (v == "true" || v == "false" || v == "null") is_str = false;
        else {
            try { std::stod(v); is_str = false; } catch (...) {}
        }
        o << "  \"" << k << "\": ";
        if (is_str) o << '"' << v << '"';
        else        o << v;
    }
    o << "\n}\n";
    return o.str();
}

} // namespace icmg::core

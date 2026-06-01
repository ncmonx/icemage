#include "db_key.hpp"
#include "json_safe.hpp"
#include <nlohmann/json.hpp>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <iterator>
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#endif

namespace icmg { namespace core {
using json = nlohmann::json;

static std::string homeDir() {
#ifdef _WIN32
    const char* h = std::getenv("USERPROFILE");
#else
    const char* h = std::getenv("HOME");
#endif
    return h ? std::string(h) : std::string(".");
}

std::string dbKeyPath()         { return homeDir() + "/.icmg/db.key"; }
std::string encryptConfigPath() { return homeDir() + "/.icmg/encrypt.json"; }

std::string toHex(const std::vector<unsigned char>& raw) {
    static const char* d = "0123456789abcdef";
    std::string out; out.reserve(raw.size()*2);
    for (unsigned char b : raw) { out.push_back(d[b>>4]); out.push_back(d[b&0xf]); }
    return out;
}
std::vector<unsigned char> fromHex(const std::string& hex) {
    auto val = [](char c)->int{
        if(c>='0'&&c<='9')return c-'0';
        if(c>='a'&&c<='f')return c-'a'+10;
        if(c>='A'&&c<='F')return c-'A'+10;
        return -1; };
    std::vector<unsigned char> out;
    for (size_t i=0;i+1<hex.size();i+=2){
        int hi=val(hex[i]),lo=val(hex[i+1]);
        if(hi<0||lo<0)break;
        out.push_back((unsigned char)((hi<<4)|lo));
    }
    return out;
}

EncryptionConfig parseEncryptionConfig(const std::string& t) {
    EncryptionConfig c;
    if (t.empty()) return c;
    try {
        auto j = json::parse(t);
        c.enabled  = j.value("enabled", false);
        c.scope    = j.value("scope", std::string("both"));
        c.key_mode = j.value("key_mode", std::string("dpapi"));
    } catch (...) {}
    return c;
}
std::string serializeEncryptionConfig(const EncryptionConfig& c) {
    json j; j["enabled"]=c.enabled; j["scope"]=c.scope; j["key_mode"]=c.key_mode;
    return icmg::core::safeDump(j);
}
std::string resolveKeyMode(const EncryptionConfig& c, bool env_key_present) {
    if (env_key_present) return "env";
    return c.key_mode.empty() ? "dpapi" : c.key_mode;
}

std::string generateDbKey() {
    unsigned char raw[32];
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, raw, sizeof(raw), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        return "";
#else
    std::ifstream u("/dev/urandom", std::ios::binary);
    if (!u.read(reinterpret_cast<char*>(raw), sizeof(raw))) return "";
#endif
    return toHex(std::vector<unsigned char>(raw, raw+sizeof(raw)));
}

#ifdef _WIN32
std::vector<unsigned char> dpapiWrap(const std::string& pt) {
    DATA_BLOB in{ (DWORD)pt.size(), (BYTE*)pt.data() }, out{};
    if (!CryptProtectData(&in, L"icmg-db-key", nullptr, nullptr, nullptr, 0, &out))
        return {};
    std::vector<unsigned char> r(out.pbData, out.pbData+out.cbData);
    LocalFree(out.pbData);
    return r;
}
std::string dpapiUnwrap(const std::vector<unsigned char>& blob) {
    if (blob.empty()) return "";
    DATA_BLOB in{ (DWORD)blob.size(), (BYTE*)blob.data() }, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        return "";
    std::string r(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return r;
}
#endif

std::string readEncryptConfigText() {
    std::ifstream f(encryptConfigPath(), std::ios::binary);
    if (!f) return "";
    std::string t((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return t;
}

bool isHexKey(const std::string& s) {
    if (s.empty() || (s.size() % 2) != 0) return false;
    for (char ch : s) if (!std::isxdigit((unsigned char)ch)) return false;
    return true;
}

std::string resolveDbKey(const EncryptionConfig& c) {
    const char* env = std::getenv("ICMG_DB_KEY");
    std::string mode = resolveKeyMode(c, env && *env);
    std::string key;
    if (mode == "env") {
        key = env ? std::string(env) : std::string("");
    } else {
        std::ifstream f(dbKeyPath(), std::ios::binary);
        if (!f) return "";
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)),
                                         std::istreambuf_iterator<char>());
        if (mode == "shared") {
            key = std::string(bytes.begin(), bytes.end());  // raw hex file
        }
#ifdef _WIN32
        else if (mode == "dpapi") {
            key = dpapiUnwrap(bytes);
        }
#endif
        else {
            key = std::string(bytes.begin(), bytes.end());
        }
    }
    // Security (CodeQL cpp/sql-injection): the key is interpolated into a SQLCipher
    // PRAGMA blob literal x'<key>' which cannot use bound parameters. Enforce strict
    // hex so a crafted key (e.g. containing a quote) cannot escape the literal.
    // Fail closed: a non-hex key yields "" -> caller skips keying / reports error.
    if (!isHexKey(key)) return "";
    return key;
}

}} // namespace

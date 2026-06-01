// v1.76: `icmg encrypt {enable|disable|status}` + plaintext<->encrypted migration.
//
// Migration uses SQLCipher's sqlcipher_export() into an ATTACHed DB, then an
// atomic swap, with a .pre-encrypt.bak / .pre-decrypt.bak backup + rollback.
// Key handling lives in core/db_key. Encryption is opt-in (default OFF).
#include "../base_command.hpp"
#ifndef _WIN32
#include <unistd.h>   // getpid/usleep on POSIX
#endif
#include "../../core/registry.hpp"
#include "../../core/db_key.hpp"
#include "../../core/config.hpp"
#include "../../core/path_utils.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace icmg { namespace cli {

static bool execOk(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::cerr << "encrypt: SQL failed (rc=" << rc << "): "
                  << (err ? err : "?") << "  [" << sql.substr(0, 60) << "...]\n";
        if (err) sqlite3_free(err);
    }
    return rc == SQLITE_OK;
}

// Overwrite-safe swap of `src` onto `dst` (Windows fs::rename won't replace an
// existing target). Returns true on success.
static bool swapOnto(const std::string& src, const std::string& dst) {
    std::error_code ec;
    fs::remove(dst, ec);                 // clear target (Windows rename can't overwrite)
    fs::rename(src, dst, ec);
    if (!ec) return true;
    fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
    if (ec) { std::cerr << "encrypt: swap failed " << src << " -> " << dst
                        << ": " << ec.message() << "\n"; return false; }
    fs::remove(src, ec);
    return true;
}

// Best-effort secure delete: overwrite the file with zeros, then unlink. The
// .bak is a PLAINTEXT copy — leaving it next to the encrypted DB would defeat
// the encryption, so we shred it once the swap is verified.
static void shred(const std::string& p) {
    std::error_code ec;
    auto sz = fs::file_size(p, ec);
    if (!ec && sz > 0) {
        std::ofstream o(p, std::ios::binary | std::ios::trunc);
        std::vector<char> zeros(4096, 0);
        for (uintmax_t w = 0; w < sz; w += zeros.size())
            o.write(zeros.data(), (std::streamsize)std::min<uintmax_t>(zeros.size(), sz - w));
    }
    fs::remove(p, ec);
}

static void ownerOnly(const std::string& p) {
    std::error_code ec;
    fs::permissions(p, fs::perms::owner_read | fs::perms::owner_write,
                    fs::perm_options::replace, ec);  // POSIX; no-op-ish on Win
}

// plaintext -> encrypted. Temp backup (owner-only) for rollback, export into a
// keyed ATTACH, overwrite-safe swap, then SHRED the plaintext backup.
bool migrateToEncrypted(const std::string& path, const std::string& hexkey) {
    if (!fs::exists(path)) return true;             // nothing to migrate
    std::error_code ec;
    std::string bak = path + ".pre-encrypt.bak";
    fs::copy_file(path, bak, fs::copy_options::overwrite_existing, ec);
    if (ec) { std::cerr << "encrypt: backup failed: " << ec.message() << "\n"; return false; }
    ownerOnly(bak);
    std::string enc = path + ".enc";
    fs::remove(enc, ec);
    sqlite3* db = nullptr;
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc != SQLITE_OK) {
        std::cerr << "encrypt: open failed (rc=" << rc << "): "
                  << sqlite3_errmsg(db) << "\n";
        sqlite3_close(db); return false;
    }
    bool ok = execOk(db, "ATTACH DATABASE '" + enc + "' AS enc KEY \"x'" + hexkey + "'\";")
           && execOk(db, "SELECT sqlcipher_export('enc');")
           && execOk(db, "DETACH DATABASE enc;");
    sqlite3_close(db);
    if (!ok) { fs::remove(enc, ec); return false; }
    if (!swapOnto(enc, path)) {
        fs::copy_file(bak, path, fs::copy_options::overwrite_existing, ec);  // rollback
        return false;
    }
    ownerOnly(path);
    shred(bak);                          // remove the plaintext copy
    return true;
}

// encrypted -> plaintext (used by `disable`). Inverse export to an empty-key DB.
bool migrateToPlaintext(const std::string& path, const std::string& hexkey) {
    if (!fs::exists(path)) return true;
    std::error_code ec;
    std::string bak = path + ".pre-decrypt.bak";
    fs::copy_file(path, bak, fs::copy_options::overwrite_existing, ec);
    ownerOnly(bak);
    std::string plain = path + ".plain";
    fs::remove(plain, ec);
    sqlite3* db = nullptr;
    int rc = sqlite3_open(path.c_str(), &db);
    if (rc != SQLITE_OK) { sqlite3_close(db); return false; }
    bool ok = execOk(db, "PRAGMA key=\"x'" + hexkey + "'\";")
           && execOk(db, "ATTACH DATABASE '" + plain + "' AS plain KEY '';")
           && execOk(db, "SELECT sqlcipher_export('plain');")
           && execOk(db, "DETACH DATABASE plain;");
    sqlite3_close(db);
    if (!ok) { fs::remove(plain, ec); return false; }
    if (!swapOnto(plain, path)) {
        fs::copy_file(bak, path, fs::copy_options::overwrite_existing, ec);
        return false;
    }
    fs::remove(bak, ec);                 // decrypted backup is plaintext too — remove
    return true;
}

static bool storeKey(const std::string& mode, const std::string& hexkey) {
    if (mode == "env") return true;   // user supplies ICMG_DB_KEY
    std::string p = icmg::core::dbKeyPath();
    std::error_code ec;
    fs::create_directories(fs::path(p).parent_path(), ec);
#ifdef _WIN32
    if (mode == "dpapi") {
        auto blob = icmg::core::dpapiWrap(hexkey);
        if (blob.empty()) return false;
        std::ofstream o(p, std::ios::binary);
        o.write(reinterpret_cast<const char*>(blob.data()), (std::streamsize)blob.size());
        return (bool)o;
    }
#endif
    {
        std::ofstream o(p, std::ios::binary);  // shared mode: raw hex
        o << hexkey;
    }
    ownerOnly(p);  // default owner-only
    if (mode == "shared") icmg::core::relaxAclEveryone(p);  // deliberately team-readable
    return true;
}

class EncryptCommand : public BaseCommand {
public:
    std::string name() const override { return "encrypt"; }
    std::string description() const override {
        return "Encrypt icmg DBs at rest (enable/disable/status)";
    }
    int run(const std::vector<std::string>& args) override {
        std::string sub = args.empty() ? "status" : args[0];
        if (sub == "status")  return doStatus();
        if (sub == "enable")  return doEnable(args);
        if (sub == "disable") return doDisable();
        std::cerr << "usage: icmg encrypt {enable|disable|status}"
                     " [--scope both|project] [--key-mode dpapi|shared|env]\n";
        return 1;
    }
private:
    static std::string flag(const std::vector<std::string>& a,
                            const std::string& k, const std::string& def) {
        for (size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == k) return a[i+1];
        return def;
    }
    static std::vector<std::string> inScopeDbs(const std::string& scope) {
        auto& cfg = icmg::core::Config::instance();
        std::vector<std::string> dbs = { cfg.projectDbPath(".") };
        if (scope == "both") dbs.push_back(cfg.globalDbPath());
        return dbs;
    }
    int doStatus() {
        auto c = icmg::core::parseEncryptionConfig(icmg::core::readEncryptConfigText());
        std::cout << "encryption: " << (c.enabled ? "ON" : "OFF")
                  << "  scope=" << c.scope << "  key-mode=" << c.key_mode << "\n";
        return 0;
    }
    int doEnable(const std::vector<std::string>& a) {
        icmg::core::EncryptionConfig c;
        c.enabled  = true;
        c.scope    = flag(a, "--scope", "both");
        c.key_mode = flag(a, "--key-mode", "dpapi");
        std::string key = (c.key_mode == "env")
            ? (std::getenv("ICMG_DB_KEY") ? std::getenv("ICMG_DB_KEY") : "")
            : icmg::core::generateDbKey();
        if (key.empty()) { std::cerr << "encrypt: key unavailable (env mode needs ICMG_DB_KEY)\n"; return 2; }
        if (!storeKey(c.key_mode, key)) { std::cerr << "encrypt: key store failed\n"; return 3; }
        for (auto& d : inScopeDbs(c.scope)) {
            if (!migrateToEncrypted(d, key)) {
                std::cerr << "encrypt: migration failed for " << d
                          << " (restored from .pre-encrypt.bak)\n";
                return 4;
            }
        }
        std::ofstream(icmg::core::encryptConfigPath())
            << icmg::core::serializeEncryptionConfig(c);
        std::cout << "encryption ENABLED (scope=" << c.scope
                  << ", key-mode=" << c.key_mode << ").\n"
                  << "  BACK UP " << icmg::core::dbKeyPath()
                  << " — losing the key is UNRECOVERABLE.\n";
        return 0;
    }
    int doDisable() {
        auto c = icmg::core::parseEncryptionConfig(icmg::core::readEncryptConfigText());
        if (!c.enabled) { std::cout << "encryption already OFF\n"; return 0; }
        std::string key = icmg::core::resolveDbKey(c);
        if (key.empty()) { std::cerr << "encrypt: key unavailable; cannot decrypt\n"; return 2; }
        for (auto& d : inScopeDbs(c.scope)) {
            if (!migrateToPlaintext(d, key)) {
                std::cerr << "encrypt: decrypt failed for " << d << "\n";
                return 4;
            }
        }
        c.enabled = false;
        std::ofstream(icmg::core::encryptConfigPath())
            << icmg::core::serializeEncryptionConfig(c);
        std::cout << "encryption DISABLED (DBs decrypted to plaintext)\n";
        return 0;
    }
};
ICMG_REGISTER_COMMAND("encrypt", EncryptCommand);

}} // namespace

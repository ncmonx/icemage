// v1.76: encryption-at-rest key management.
// Resolves the per-user DB key for SQLCipher's PRAGMA key, across three modes:
//   dpapi  (default) — random key sealed to the Windows account via DPAPI
//   shared           — raw hex key file with team-readable ACL (shared-server)
//   env              — ICMG_DB_KEY (CI / server)
#pragma once
#include <string>
#include <vector>

namespace icmg { namespace core {

struct EncryptionConfig {
    bool        enabled  = false;
    std::string scope    = "both";   // "both" | "project"
    std::string key_mode = "dpapi";  // "dpapi" | "shared" | "env"
};

std::string toHex(const std::vector<unsigned char>& raw);
std::vector<unsigned char> fromHex(const std::string& hex);

// Parse ~/.icmg/encrypt.json content. Empty/invalid -> disabled config.
EncryptionConfig parseEncryptionConfig(const std::string& json_text);
std::string serializeEncryptionConfig(const EncryptionConfig& c);

// Effective key-mode: ICMG_DB_KEY present -> "env"; else the config's mode.
std::string resolveKeyMode(const EncryptionConfig& c, bool env_key_present);

std::string generateDbKey();   // 32 random bytes -> 64 hex chars
std::string dbKeyPath();       // ~/.icmg/db.key
std::string encryptConfigPath();   // ~/.icmg/encrypt.json

#ifdef _WIN32
std::vector<unsigned char> dpapiWrap(const std::string& plaintext);
std::string dpapiUnwrap(const std::vector<unsigned char>& blob);
#endif

// Resolve the raw hex key per the config's mode. "" if unavailable (fail closed).
std::string resolveDbKey(const EncryptionConfig& c);

// Read ~/.icmg/encrypt.json (returns "" if absent). Used by Db ctor + cmd.
std::string readEncryptConfigText();

}} // namespace

#pragma once
// Force OpenSSL's RNG onto Windows BCryptGenRandom.
//
// SQLCipher (OpenSSL provider) draws random bytes on every encrypted write
// (per-page IV/salt). On some headless Windows Server 2019 / Server Core SKUs,
// OpenSSL's default Windows entropy path falls through to the legacy CryptoAPI
// provider (rsaenh -> cryptbase -> ntmarta) whose module is NOT installed ->
// LoadLibrary fails with err126 ("specified module could not be found") and
// `icmg context` / `graph update` crash on the write side.
//
// BCryptGenRandom (bcrypt.dll / CNG) is self-contained and present on every
// modern Windows incl. Server Core -- icmg already uses it for db_key + server
// tokens. We install a RAND_METHOD backed by it so OpenSSL never touches the
// missing CryptoAPI module. No-op (returns false) off Windows.
#include <cstddef>

namespace icmg::core {

// Fill buf with n cryptographically-strong bytes via BCryptGenRandom.
// Returns false on failure or off Windows. Exposed for unit testing.
bool bcryptFill(unsigned char* buf, std::size_t n);

// Install the BCrypt-backed RAND_METHOD as OpenSSL's RNG. Probes BCrypt first
// and only overrides if it works. Returns true on success. Call once at startup
// BEFORE any encrypted DB is opened. No-op (false) off Windows.
bool installBCryptOpenSSLRand();

}  // namespace icmg::core

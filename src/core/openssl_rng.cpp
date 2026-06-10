#include "openssl_rng.hpp"

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <bcrypt.h>
#  pragma comment(lib, "bcrypt.lib")
#  ifndef BCRYPT_USE_SYSTEM_PREFERRED_RNG
#    define BCRYPT_USE_SYSTEM_PREFERRED_RNG 0x00000002
#  endif
// RAND_set_rand_method is deprecated in OpenSSL 3.x but still functional and is
// the supported way to override RAND_bytes() for legacy consumers like SQLCipher.
#  ifndef OPENSSL_SUPPRESS_DEPRECATED
#    define OPENSSL_SUPPRESS_DEPRECATED
#  endif
#  include <openssl/rand.h>

namespace icmg::core {

bool bcryptFill(unsigned char* buf, std::size_t n) {
    if (n == 0) return true;
    if (!buf) return false;
    NTSTATUS st = BCryptGenRandom(nullptr, buf, (ULONG)n,
                                  BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    return st == 0;  // STATUS_SUCCESS
}

namespace {

// OpenSSL RAND_METHOD callbacks (1 = success, 0 = failure in OpenSSL's ABI).
int ossl_bytes(unsigned char* buf, int num) {
    if (num < 0) return 0;
    return bcryptFill(buf, (std::size_t)num) ? 1 : 0;
}
int ossl_status() { return 1; }  // system RNG is always seeded

RAND_METHOD g_bcrypt_method = {
    nullptr,      // seed   (no-op: system RNG self-seeds)
    ossl_bytes,   // bytes
    nullptr,      // cleanup
    nullptr,      // add
    ossl_bytes,   // pseudorand
    ossl_status   // status
};

}  // namespace

bool installBCryptOpenSSLRand() {
    unsigned char probe[16];
    if (!bcryptFill(probe, sizeof(probe))) return false;  // BCrypt unusable -> leave OpenSSL as-is
    return RAND_set_rand_method(&g_bcrypt_method) == 1;
}

}  // namespace icmg::core

#else  // !_WIN32

namespace icmg::core {
bool bcryptFill(unsigned char*, std::size_t) { return false; }
bool installBCryptOpenSSLRand() { return false; }
}  // namespace icmg::core

#endif

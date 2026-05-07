// Phase 23: shared embedder helpers (pack/unpack/cosine/hash).
#include "embedder.hpp"
#include <cstring>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace icmg::embed {

std::vector<uint8_t> packVec(const std::vector<float>& v) {
    std::vector<uint8_t> out(v.size() * sizeof(float));
    if (!v.empty()) std::memcpy(out.data(), v.data(), out.size());
    return out;
}

std::vector<float> unpackVec(const std::vector<uint8_t>& blob, int dim) {
    std::vector<float> out(dim, 0.0f);
    size_t bytes = static_cast<size_t>(dim) * sizeof(float);
    if (blob.size() < bytes) return out;
    std::memcpy(out.data(), blob.data(), bytes);
    return out;
}

float cosine(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.empty() || a.size() != b.size()) return 0.0f;
    double dot = 0.0, na = 0.0, nb = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        dot += static_cast<double>(a[i]) * b[i];
        na  += static_cast<double>(a[i]) * a[i];
        nb  += static_cast<double>(b[i]) * b[i];
    }
    if (na == 0.0 || nb == 0.0) return 0.0f;
    return static_cast<float>(dot / (std::sqrt(na) * std::sqrt(nb)));
}

std::string fnv1a64(const std::string& s) {
    uint64_t h = 1469598103934665603ULL;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    std::ostringstream os;
    os << std::hex << std::setw(16) << std::setfill('0') << h;
    return os.str();
}

} // namespace icmg::embed

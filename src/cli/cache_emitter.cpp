#include "cache_emitter.hpp"
#include <sstream>

namespace icmg::cli {

bool hasCacheWrap(const std::string& text) {
    return text.find("<<CACHED") != std::string::npos
        && text.find("<</CACHED>>") != std::string::npos;
}

std::string wrapCachePrefix(const std::string& text, const CacheEmitOptions& opts) {
    if (text.empty() || hasCacheWrap(text)) return text;
    std::ostringstream os;
    os << "<<CACHED ttl=" << opts.ttl_seconds << ">>\n"
       << text;
    if (text.empty() || text.back() != '\n') os << "\n";
    os << "<</CACHED>>\n";
    return os.str();
}

} // namespace icmg::cli

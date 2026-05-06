#include "base_importer.hpp"
#include <fstream>
#include <filesystem>

namespace icmg {

bool BaseImporter::isSQLiteFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char magic[16] = {};
    f.read(magic, 16);
    // SQLite header: "SQLite format 3\0"
    return std::string(magic, 15) == "SQLite format 3";
}

void BaseImporter::checkFileSize(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    auto sz = fs::file_size(path, ec);
    if (!ec && sz > 100ULL * 1024 * 1024)
        throw ImportError("Import file too large (max 100MB): " + path);
}

} // namespace icmg

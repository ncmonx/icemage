#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Fuzz the markdown section parser logic used in icmg claudemd import.
// Standalone — no icmg source required.

static std::vector<std::string> parseSections(const std::string& text) {
    std::vector<std::string> titles;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = (nl == std::string::npos)
            ? text.substr(pos)
            : text.substr(pos, nl - pos);
        if (line.size() >= 3 && line[0] == '#' && line[1] == '#' && line[2] == ' ')
            titles.push_back(line.substr(3));
        pos = (nl == std::string::npos) ? text.size() : nl + 1;
    }
    return titles;
}

static std::string slugify(const std::string& title) {
    std::string slug;
    for (unsigned char c : title) {
        if (std::isalnum(c))
            slug += static_cast<char>(std::tolower(c));
        else if (c == ' ' || c == '_' || c == '-')
            if (!slug.empty() && slug.back() != '-') slug += '-';
    }
    while (!slug.empty() && slug.back() == '-') slug.pop_back();
    return slug;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 65536) return 0;
    std::string input(reinterpret_cast<const char*>(data), size);
    auto sections = parseSections(input);
    for (const auto& s : sections)
        (void)slugify(s);
    return 0;
}

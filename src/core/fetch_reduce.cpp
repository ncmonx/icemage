#include "fetch_reduce.hpp"
#include <regex>
#include <sstream>

namespace icmg::core {

std::string reduceHtml(const std::string& body, size_t cap) {
    std::string s = body;
    s = std::regex_replace(s,
        std::regex(R"(<script\b[^>]*>[\s\S]*?</script>)", std::regex::icase), "");
    s = std::regex_replace(s,
        std::regex(R"(<style\b[^>]*>[\s\S]*?</style>)", std::regex::icase), "");
    for (auto* tag : {"nav", "aside", "footer", "header"}) {
        s = std::regex_replace(s,
            std::regex(std::string("<") + tag + R"(\b[^>]*>[\s\S]*?</)" + tag + ">",
                       std::regex::icase), "");
    }
    std::string title;
    {
        std::smatch m;
        std::regex re(R"(<title[^>]*>([\s\S]*?)</title>)", std::regex::icase);
        if (std::regex_search(s, m, re) && m.size() >= 2) title = m[1];
    }
    std::string main_body;
    {
        std::smatch m;
        std::regex re_main(R"(<main\b[^>]*>([\s\S]*?)</main>)", std::regex::icase);
        std::regex re_art(R"(<article\b[^>]*>([\s\S]*?)</article>)", std::regex::icase);
        if (std::regex_search(s, m, re_main) && m.size() >= 2) main_body = m[1];
        else if (std::regex_search(s, m, re_art) && m.size() >= 2) main_body = m[1];
        else main_body = s;
    }
    main_body = std::regex_replace(main_body, std::regex(R"(<[^>]+>)"), " ");
    struct E { const char* k; const char* v; };
    const E ents[] = {{"&amp;","&"},{"&lt;","<"},{"&gt;",">"},{"&quot;","\""},
                      {"&#39;","'"},{"&apos;","'"},{"&nbsp;"," "}};
    for (auto& e : ents) {
        main_body = std::regex_replace(main_body, std::regex(e.k), e.v);
    }
    main_body = std::regex_replace(main_body, std::regex(R"(\s+)"), " ");
    if (!main_body.empty() && main_body.front() == ' ') main_body.erase(0, 1);
    if (!main_body.empty() && main_body.back() == ' ') main_body.pop_back();
    std::ostringstream os;
    if (!title.empty()) os << "# " << title << "\n\n";
    os << main_body;
    std::string out = os.str();
    if (out.size() > cap) out = out.substr(0, cap) + "\n... [truncated]";
    return out;
}

} // namespace icmg::core

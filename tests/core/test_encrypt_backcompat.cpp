// v1.76 T4: encryption OFF (default) -> plaintext DB opens/works as before.
#include "../test_main.hpp"
#include "../../src/core/db.hpp"
#include <filesystem>

TEST("encrypt-backcompat: plaintext DB works when encryption disabled") {
    namespace fs = std::filesystem;
    auto p = (fs::temp_directory_path() / "icmg_bc.db").string();
    std::error_code ec; fs::remove(p, ec);
    {
        icmg::core::Db db(p);
        db.run("CREATE TABLE t(x TEXT)");
        db.run("INSERT INTO t VALUES(?)", {"hello"});
        std::string got;
        db.query("SELECT x FROM t", {}, [&](const icmg::core::Row& r){
            if (!r.empty()) got = r[0];
        });
        ASSERT_EQ(got, std::string("hello"));
    }
    fs::remove(p, ec);
}

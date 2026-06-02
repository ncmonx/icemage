// v2.0.0 externals (API Spec Compilation): collapse a verbose OpenAPI JSON into a
// dense endpoint map (METHOD /path — summary (N params)) so an agent reads an API
// surface in a fraction of the tokens. Pure (nlohmann json in, string out).

#include "../test_main.hpp"
#include "../../src/core/api_spec.hpp"

#include <string>

using namespace icmg::core;

namespace {
const char* SPEC = R"JSON({
  "openapi": "3.0.0",
  "info": {"title": "Demo", "version": "1.0"},
  "paths": {
    "/users": {
      "get":  {"summary": "List users", "parameters": [{"name":"page"},{"name":"size"}]},
      "post": {"summary": "Create user"}
    },
    "/users/{id}": {
      "delete": {"summary": "Delete user", "parameters": [{"name":"id"}]}
    }
  }
})JSON";
}

TEST("api spec: emits METHOD path + summary lines") {
    auto s = compactOpenApi(SPEC);
    ASSERT_CONTAINS(s, std::string("GET /users"));
    ASSERT_CONTAINS(s, std::string("List users"));
    ASSERT_CONTAINS(s, std::string("POST /users"));
    ASSERT_CONTAINS(s, std::string("DELETE /users/{id}"));
}

TEST("api spec: counts parameters") {
    auto s = compactOpenApi(SPEC);
    ASSERT_CONTAINS(s, std::string("2 params"));   // GET /users
}

TEST("api spec: much smaller than source") {
    auto s = compactOpenApi(SPEC);
    ASSERT_TRUE(s.size() < std::string(SPEC).size());
}

TEST("api spec: invalid json -> empty") {
    ASSERT_EQ(compactOpenApi("not json {{{"), std::string(""));
}

TEST("api spec: no paths -> empty") {
    ASSERT_EQ(compactOpenApi("{\"openapi\":\"3.0.0\"}"), std::string(""));
}

#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif

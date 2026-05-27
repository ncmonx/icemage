#include "cli/router.hpp"
#include <cassert>
#include <iostream>

using namespace icmg::cli;

static void test_short_id_question_local() {
    auto r = classifyPrompt("apa itu rekursi");
    assert(r.route == Route::LOCAL);
}

static void test_short_en_question_local() {
    auto r = classifyPrompt("what is REST");
    assert(r.route == Route::LOCAL);
}

static void test_tool_verb_cloud() {
    auto r = classifyPrompt("refactor src/foo.cpp to use std::unique_ptr");
    assert(r.route == Route::CLOUD);
}

static void test_build_verb_cloud() {
    auto r = classifyPrompt("debug failing CI build on main branch");
    assert(r.route == Route::CLOUD);
}

static void test_long_default_cloud() {
    std::string longp(500, 'x');
    auto r = classifyPrompt(longp);
    assert(r.route == Route::CLOUD);
}

static void test_empty_default_cloud() {
    auto r = classifyPrompt("");
    assert(r.route == Route::CLOUD);
}

#ifndef ICMG_MONO_TEST
int main() {
    test_short_id_question_local();
    test_short_en_question_local();
    test_tool_verb_cloud();
    test_build_verb_cloud();
    test_long_default_cloud();
    test_empty_default_cloud();
    std::cout << "test_router: 6/6 PASS\n";
    return 0;
}

#endif  // ICMG_MONO_TEST

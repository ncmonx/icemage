// tests/graph/test_hcl_extractor.cpp
//
// TDD tests for HclExtractor (gap G4 vs graphify: HCL/Terraform graph).
//
// Encoding conventions:
//   classes   << "<type>.<name>"     -> resource/data/module/variable/output/provider blocks
//   functions << "<name>"            -> the block's local name (quick lookup)
//   imports   << "module:<source>"   -> module block source (dependency edge)

#include "../test_main.hpp"
#include "../../src/graph/extractor/base_extractor.hpp"
#include "../../src/core/registry.hpp"
#include <string>
#include <algorithm>

static icmg::graph::BaseExtractor* getHclExt() {
    static auto inst = []() -> std::unique_ptr<icmg::graph::BaseExtractor> {
        auto& reg = icmg::core::Registry<icmg::graph::BaseExtractor>::instance();
        if (!reg.has("hcl")) return nullptr;
        return reg.create("hcl");
    }();
    return inst.get();
}

static bool has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

TEST("hcl-extractor: registered") {
    ASSERT_TRUE(getHclExt() != nullptr);
}

TEST("hcl-extractor: resource block -> type.name in classes") {
    auto* ext = getHclExt();
    ASSERT_TRUE(ext != nullptr);
    std::string src =
        "resource \"aws_instance\" \"web\" {\n"
        "  ami = \"ami-123\"\n"
        "}\n";
    auto r = ext->extract("main.tf", src);
    ASSERT_TRUE(has(r.classes, "resource.aws_instance.web"));
    ASSERT_TRUE(has(r.functions, "web"));
}

TEST("hcl-extractor: variable / output / data blocks") {
    auto* ext = getHclExt();
    ASSERT_TRUE(ext != nullptr);
    std::string src =
        "variable \"region\" {\n  default = \"us-east-1\"\n}\n"
        "output \"ip\" {\n  value = 1\n}\n"
        "data \"aws_ami\" \"ubuntu\" {\n  owners = [\"self\"]\n}\n";
    auto r = ext->extract("main.tf", src);
    ASSERT_TRUE(has(r.classes, "variable.region"));
    ASSERT_TRUE(has(r.classes, "output.ip"));
    ASSERT_TRUE(has(r.classes, "data.aws_ami.ubuntu"));
}

TEST("hcl-extractor: module source -> import edge") {
    auto* ext = getHclExt();
    ASSERT_TRUE(ext != nullptr);
    std::string src =
        "module \"vpc\" {\n"
        "  source  = \"terraform-aws-modules/vpc/aws\"\n"
        "  version = \"5.0.0\"\n"
        "}\n";
    auto r = ext->extract("main.tf", src);
    ASSERT_TRUE(has(r.classes, "module.vpc"));
    ASSERT_TRUE(has(r.imports, "module:terraform-aws-modules/vpc/aws"));
}

TEST("hcl-extractor: provider block") {
    auto* ext = getHclExt();
    ASSERT_TRUE(ext != nullptr);
    std::string src = "provider \"aws\" {\n  region = \"eu-west-1\"\n}\n";
    auto r = ext->extract("main.tf", src);
    ASSERT_TRUE(has(r.classes, "provider.aws"));
}

TEST("hcl-extractor: comments (# and //) ignored") {
    auto* ext = getHclExt();
    ASSERT_TRUE(ext != nullptr);
    std::string src =
        "# resource \"commented\" \"out\" {}\n"
        "// resource \"also\" \"out\" {}\n"
        "resource \"aws_s3_bucket\" \"real\" {\n  bucket = \"x\"\n}\n";
    auto r = ext->extract("main.tf", src);
    ASSERT_TRUE(has(r.classes, "resource.aws_s3_bucket.real"));
    ASSERT_TRUE(!has(r.classes, "resource.commented.out"));
    ASSERT_TRUE(!has(r.classes, "resource.also.out"));
}

TEST("hcl-extractor: extensions include .tf and .hcl") {
    auto* ext = getHclExt();
    ASSERT_TRUE(ext != nullptr);
    ASSERT_TRUE(has(ext->extensions(), ".tf"));
    ASSERT_TRUE(has(ext->extensions(), ".hcl"));
}

TEST("hcl-extractor: no crash on empty / junk input") {
    auto* ext = getHclExt();
    ASSERT_TRUE(ext != nullptr);
    auto r1 = ext->extract("e.tf", "");
    auto r2 = ext->extract("e.tf", "}}} garbage {{{ = = ");
    (void)r1; (void)r2;
}

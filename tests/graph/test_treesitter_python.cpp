#include "../test_main.hpp"
#include "../../src/core/registry.hpp"
#include "../../src/graph/symbol_extractor/base_symbol_extractor.hpp"

using icmg::graph::BaseSymbolExtractor;
using icmg::graph::Symbol;
using Reg = icmg::core::Registry<BaseSymbolExtractor>;

#ifdef ICMG_HAS_TREESITTER_PY

TEST("treesitter-py: function + class + method + inheritance") {
    auto e = Reg::instance().create("ast-py");
    ASSERT_TRUE(e != nullptr);
    std::string src =
        "class Animal:\n"
        "    def __init__(self, name):\n"
        "        self.name = name\n"
        "    def speak(self):\n"
        "        return self.name\n"
        "\n"
        "class Dog(Animal):\n"
        "    def bark(self):\n"
        "        print('woof')\n"
        "\n"
        "def helper(x):\n"
        "    return x * 2\n"
        "\n"
        "@property\n"
        "def computed():\n"
        "    return 42\n";
    auto syms = e->extractSymbols("a.py", src);

    bool ani=false, dog=false, dog_bark=false, ani_speak=false, hlp=false, comp=false;
    bool dog_inherits = false;
    for (auto& s : syms) {
        if (s.kind == "class" && s.name == "Animal") ani = true;
        if (s.kind == "class" && s.name == "Dog") {
            dog = true;
            for (auto& b : s.bases) if (b == "Animal") dog_inherits = true;
        }
        if (s.kind == "method"   && s.name == "Dog.bark")    dog_bark = true;
        if (s.kind == "method"   && s.name == "Animal.speak") ani_speak = true;
        if (s.kind == "function" && s.name == "helper")      hlp = true;
        if (s.kind == "function" && s.name == "computed")    comp = true;
    }
    ASSERT_TRUE(ani); ASSERT_TRUE(dog); ASSERT_TRUE(dog_inherits);
    ASSERT_TRUE(dog_bark); ASSERT_TRUE(ani_speak);
    ASSERT_TRUE(hlp); ASSERT_TRUE(comp);
}

TEST("treesitter-py: empty + garbage no crash") {
    auto e = Reg::instance().create("ast-py");
    ASSERT_TRUE(e != nullptr);
    auto a = e->extractSymbols("x.py", "");
    ASSERT_EQ((int)a.size(), 0);
    auto b = e->extractSymbols("x.py", "@@@ not py @@@");
    (void)b;
}

#else

TEST("treesitter-py: build skipped") { ASSERT_TRUE(true); }

#endif


#ifndef ICMG_MONO_TEST
int main() { return icmg::test::run_all(); }
#endif

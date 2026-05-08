// Phase 34: WordPiece tokenizer behavioral tests.
// Independent of ONNX — these run on every build.
#include "../test_main.hpp"
#include "../../src/embed/wordpiece_tokenizer.hpp"
#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;
using icmg::embed::WordPieceTokenizer;

// Build a minimal vocab fixture file mirroring BERT base-uncased layout
// for the tokens we care about in tests. Real vocab.txt has 30522 lines;
// we only need the special tokens + the ones our test sentences use.
static std::string makeFixtureVocab() {
    auto p = fs::temp_directory_path() / ("wp_vocab_" + std::to_string(::time(nullptr))
                                           + "_" + std::to_string(rand()) + ".txt");
    std::ofstream out(p);
    // Lines 0..102 must hit special-token IDs at correct positions.
    // We pad with placeholder tokens to reach the right line numbers.
    for (int i = 0; i < 103; ++i) {
        if      (i == 0)   out << "[PAD]\n";
        else if (i == 100) out << "[UNK]\n";
        else if (i == 101) out << "[CLS]\n";
        else if (i == 102) out << "[SEP]\n";
        else               out << "_RESERVED_" << i << "\n";
    }
    // Now real tokens for "hello world fix login bug auth"
    out << "hello\n";       // 103
    out << "world\n";       // 104
    out << "fix\n";         // 105
    out << "login\n";       // 106
    out << "bug\n";         // 107
    out << "auth\n";        // 108
    out << "is\n";          // 109
    out << "##sue\n";       // 110 (continuation for "issue")
    out << "is##\n";        // (not used; continuation pieces use ## prefix)
    out.close();
    return p.string();
}

TEST("wordpiece: load vocab + special token ids") {
    auto path = makeFixtureVocab();
    WordPieceTokenizer t;
    ASSERT_TRUE(t.loadVocab(path));
    ASSERT_TRUE(t.ready());
    ASSERT_EQ(t.lookupId("[CLS]"), (int64_t)101);
    ASSERT_EQ(t.lookupId("[SEP]"), (int64_t)102);
    ASSERT_EQ(t.lookupId("[UNK]"), (int64_t)100);
    ASSERT_EQ(t.lookupId("[PAD]"), (int64_t)0);
    fs::remove(path);
}

TEST("wordpiece: encode 'hello world' wraps with CLS/SEP + pads") {
    auto path = makeFixtureVocab();
    WordPieceTokenizer t;
    t.loadVocab(path);
    auto out = t.encode("hello world", 8);
    ASSERT_EQ((int)out.input_ids.size(), 8);
    ASSERT_EQ(out.input_ids[0], (int64_t)101);   // CLS
    ASSERT_EQ(out.input_ids[1], (int64_t)103);   // hello
    ASSERT_EQ(out.input_ids[2], (int64_t)104);   // world
    ASSERT_EQ(out.input_ids[3], (int64_t)102);   // SEP
    ASSERT_EQ(out.input_ids[4], (int64_t)0);     // PAD
    ASSERT_EQ(out.attention_mask[0], (int64_t)1);
    ASSERT_EQ(out.attention_mask[3], (int64_t)1);
    ASSERT_EQ(out.attention_mask[4], (int64_t)0);
    fs::remove(path);
}

TEST("wordpiece: lowercase normalization") {
    auto path = makeFixtureVocab();
    WordPieceTokenizer t;
    t.loadVocab(path);
    auto a = t.encode("HELLO WORLD", 8);
    auto b = t.encode("hello world", 8);
    ASSERT_TRUE(a.input_ids == b.input_ids);
    fs::remove(path);
}

TEST("wordpiece: unknown word -> UNK") {
    auto path = makeFixtureVocab();
    WordPieceTokenizer t;
    t.loadVocab(path);
    auto out = t.encode("hello xyzqq", 6);
    // Order: CLS hello [unknown for xyzqq -> UNK] SEP PAD ...
    ASSERT_EQ(out.input_ids[0], (int64_t)101);
    ASSERT_EQ(out.input_ids[1], (int64_t)103);  // hello
    ASSERT_EQ(out.input_ids[2], (int64_t)100);  // UNK for xyzqq
    ASSERT_EQ(out.input_ids[3], (int64_t)102);  // SEP
    fs::remove(path);
}

TEST("wordpiece: punctuation split") {
    auto words = WordPieceTokenizer::splitWhitespacePunct("hello, world!");
    // Expect: hello , world !
    ASSERT_EQ((int)words.size(), 4);
    ASSERT_EQ(words[0], std::string("hello"));
    ASSERT_EQ(words[1], std::string(","));
    ASSERT_EQ(words[2], std::string("world"));
    ASSERT_EQ(words[3], std::string("!"));
}

TEST("wordpiece: greedy longest-prefix continuation") {
    auto path = makeFixtureVocab();
    WordPieceTokenizer t;
    t.loadVocab(path);
    // "issue" should split into "is" + "##sue" via greedy longest match.
    auto ids = t.wordpieceSplit("issue");
    ASSERT_EQ((int)ids.size(), 2);
    ASSERT_EQ(ids[0], (int64_t)109);   // is
    ASSERT_EQ(ids[1], (int64_t)110);   // ##sue
    fs::remove(path);
}

TEST("wordpiece: max_len truncation") {
    auto path = makeFixtureVocab();
    WordPieceTokenizer t;
    t.loadVocab(path);
    // 10 words but max_len=5: CLS + 3 tokens + SEP
    auto out = t.encode("hello world fix login bug auth", 5);
    ASSERT_EQ((int)out.input_ids.size(), 5);
    ASSERT_EQ(out.input_ids[0], (int64_t)101);   // CLS
    ASSERT_EQ(out.input_ids[4], (int64_t)102);   // SEP at end
    // Real tokens 1..3 should be hello/world/fix.
    fs::remove(path);
}

TEST("wordpiece: missing vocab returns false") {
    WordPieceTokenizer t;
    ASSERT_FALSE(t.loadVocab("/nonexistent/vocab.txt"));
    ASSERT_FALSE(t.ready());
}

int main() {
    std::cout << "=== WordPiece tokenizer tests ===\n";
    return icmg::test::run_all();
}

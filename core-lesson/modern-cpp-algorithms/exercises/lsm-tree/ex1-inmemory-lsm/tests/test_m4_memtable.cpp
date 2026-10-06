// M4 — Internal keys & memtable. Notes Part 5 §4, Part 6 §6.
#include <map>
#include <random>
#include <string>

#include "helpers.hpp"
#include "lsm/internal_key.hpp"
#include "lsm/memtable.hpp"
#include "minitest.hpp"

using namespace lsm;
using testutil::Hex;

TEST(internal_key_encoding) {
    CHECK_EQ(PackSequenceAndType(5, ValueType::kValue), 0x501ull);
    CHECK_EQ(PackSequenceAndType(12, ValueType::kDeletion), 0xC00ull);
    std::string ik = MakeInternalKey("k", 9, ValueType::kValue);
    CHECK_EQ(Hex(ik), std::string("6b 01 09 00 00 00 00 00 00"));
    ParsedInternalKey p;
    CHECK(ParseInternalKey(ik, &p));
    CHECK_EQ(p.user_key, std::string_view("k"));
    CHECK_EQ(p.sequence, 9ull);
    CHECK(p.type == ValueType::kValue);
    CHECK_EQ(ExtractUserKey(ik), std::string_view("k"));
    CHECK(!ParseInternalKey("short", &p));
    std::string bad = "k";
    bad.append("\x07\x01\x00\x00\x00\x00\x00\x00", 8);  // type 7 is not a ValueType
    CHECK(!ParseInternalKey(bad, &p));
}

TEST(internal_key_order) {
    // Part 5 §4 example: newest version of "k" first.
    const std::string del12 = MakeInternalKey("k", 12, ValueType::kDeletion);
    const std::string val9 = MakeInternalKey("k", 9, ValueType::kValue);
    const std::string val5 = MakeInternalKey("k", 5, ValueType::kValue);
    const std::string l100 = MakeInternalKey("l", 100, ValueType::kValue);
    CHECK(InternalKeyCompare(del12, val9) < 0);
    CHECK(InternalKeyCompare(val9, val5) < 0);
    CHECK(InternalKeyCompare(val5, l100) < 0);
    CHECK(InternalKeyCompare(val9, val9) == 0);
    // a proper prefix sorts first, bytes compare UNSIGNED
    CHECK(InternalKeyCompare(MakeInternalKey("a", 1, ValueType::kValue), MakeInternalKey("ab", 1, ValueType::kValue)) < 0);
    CHECK(BytewiseCompare("\x01", "\xff") < 0);
    CHECK(BytewiseCompare("10", "9") < 0);  // Part 2 §4: strings are not numbers
}

TEST(internal_key_separators) {
    std::string s = "the quick brown fox";
    FindShortestSeparator(&s, "the who");
    CHECK_EQ(s, std::string("the r"));       // Part 6 §6 worked example
    s = "abc";
    FindShortestSeparator(&s, "abd");
    CHECK_EQ(s, std::string("abc"));         // 'c'+1 == 'd' is not < 'd'
    s = "abc1";
    FindShortestSeparator(&s, "abcd");
    CHECK_EQ(s, std::string("abc2"));
    s = "abc";
    FindShortestSeparator(&s, "abcdef");     // prefix: do not shorten
    CHECK_EQ(s, std::string("abc"));
    s = "abc";
    FindShortSuccessor(&s);
    CHECK_EQ(s, std::string("b"));
    s = "\xff\xff";
    FindShortSuccessor(&s);
    CHECK_EQ(s, std::string("\xff\xff"));
}

TEST(memtable_versions_and_tombstones) {
    MemTable mem;
    mem.Add(1, ValueType::kValue, "k", "a");
    mem.Add(2, ValueType::kValue, "k", "b");
    mem.Add(3, ValueType::kDeletion, "k", "");
    mem.Add(4, ValueType::kValue, "ka", "prefix-neighbour");
    std::string v;
    CHECK(mem.Get("k", 3, &v) == LookupResult::kDeleted);
    CHECK(mem.Get("k", 2, &v) == LookupResult::kFound);
    CHECK_EQ(v, std::string("b"));
    CHECK(mem.Get("k", 1, &v) == LookupResult::kFound);
    CHECK_EQ(v, std::string("a"));
    CHECK(mem.Get("k", 0, &v) == LookupResult::kNotPresent);
    CHECK(mem.Get("j", 100, &v) == LookupResult::kNotPresent);
    CHECK(mem.Get("ka", 100, &v) == LookupResult::kFound);
    CHECK_EQ(v, std::string("prefix-neighbour"));
    CHECK(mem.Get("kb", 100, &v) == LookupResult::kNotPresent);
}

TEST(memtable_iterator_order_and_binary_safety) {
    MemTable mem;
    std::map<std::string, std::string, bool (*)(const std::string&, const std::string&)> expect(
        [](const std::string& a, const std::string& b) { return InternalKeyCompare(a, b) < 0; });
    std::mt19937_64 rng(5);
    for (SequenceNumber seq = 1; seq <= 3000; ++seq) {
        std::string key = testutil::RandomBytes(rng, 1 + rng() % 6);  // may contain '\0'
        std::string val = testutil::RandomBytes(rng, rng() % 40);
        ValueType t = (rng() % 5 == 0) ? ValueType::kDeletion : ValueType::kValue;
        if (t == ValueType::kDeletion) val.clear();
        mem.Add(seq, t, key, val);
        expect.emplace(MakeInternalKey(key, seq, t), val);
    }
    auto it = mem.NewIterator();
    it->SeekToFirst();
    for (const auto& [k, v] : expect) {
        CHECK(it->Valid());
        CHECK_EQ(it->key(), std::string_view(k));
        CHECK_EQ(it->value(), std::string_view(v));
        it->Next();
    }
    CHECK(!it->Valid());
    CHECK(mem.ApproximateMemoryUsage() > 3000);
}

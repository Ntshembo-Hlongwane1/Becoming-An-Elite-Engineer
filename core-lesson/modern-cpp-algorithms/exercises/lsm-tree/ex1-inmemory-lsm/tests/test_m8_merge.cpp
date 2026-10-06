// M8 — Merging iterator and DB iterator. Notes Part 3 §6, Part 5 §4, §6.
#include <algorithm>
#include <random>
#include <string>
#include <vector>

#include "lsm/iterator.hpp"
#include "minitest.hpp"

using namespace lsm;
using KV = std::vector<std::pair<std::string, std::string>>;

namespace {
std::vector<std::pair<std::string, std::string>> Drain(Iterator* it) {
    std::vector<std::pair<std::string, std::string>> out;
    for (it->SeekToFirst(); it->Valid(); it->Next()) out.emplace_back(std::string(it->key()), std::string(it->value()));
    return out;
}
}  // namespace

TEST(merge_three_sources_with_ties) {
    for (bool heap : {false, true}) {
        std::vector<std::unique_ptr<Iterator>> kids;
        kids.push_back(NewVectorIterator(KV{{"a", "0"}, {"c", "0"}, {"e", "0"}}, BytewiseCompare));
        kids.push_back(NewVectorIterator(KV{{"b", "1"}, {"c", "1"}}, BytewiseCompare));
        kids.push_back(NewVectorIterator(KV{}, BytewiseCompare));
        kids.push_back(NewVectorIterator(KV{{"c", "3"}, {"d", "3"}}, BytewiseCompare));
        auto m = NewMergingIterator(BytewiseCompare, std::move(kids), heap);
        auto got = Drain(m.get());
        KV want{{"a", "0"}, {"b", "1"}, {"c", "0"}, {"c", "1"}, {"c", "3"}, {"d", "3"}, {"e", "0"}};
        CHECK(got == want);
        m->Seek("c");
        CHECK(m->Valid());
        CHECK_EQ(m->key(), std::string_view("c"));
        CHECK_EQ(m->value(), std::string_view("0"));  // lowest child index first
        m->Seek("ca");
        CHECK_EQ(m->key(), std::string_view("d"));
    }
}

TEST(merge_random_matches_sort) {
    std::mt19937_64 rng(8);
    for (bool heap : {false, true}) {
        std::vector<std::unique_ptr<Iterator>> kids;
        KV all;
        for (int c = 0; c < 17; ++c) {
            KV kv;
            for (int i = 0; i < 200; ++i) kv.emplace_back("k" + std::to_string(rng() % 100000), std::to_string(c));
            std::sort(kv.begin(), kv.end(), [](auto& a, auto& b) { return a.first < b.first; });
            for (auto& e : kv) all.push_back(e);
            kids.push_back(NewVectorIterator(kv, BytewiseCompare));
        }
        std::stable_sort(all.begin(), all.end(), [](auto& a, auto& b) { return a.first < b.first; });
        auto m = NewMergingIterator(BytewiseCompare, std::move(kids), heap);
        auto got = Drain(m.get());
        CHECK_EQ(got.size(), all.size());
        for (std::size_t i = 0; i < got.size(); ++i) CHECK_EQ(got[i].first, all[i].first);
    }
}

TEST(db_iterator_snapshot_view) {
    // internal entries, already in internal-key order
    KV entries{
        {MakeInternalKey("a", 5, ValueType::kValue), "a5"},
        {MakeInternalKey("b", 9, ValueType::kDeletion), ""},
        {MakeInternalKey("b", 4, ValueType::kValue), "b4"},
        {MakeInternalKey("c", 7, ValueType::kValue), "c7"},
        {MakeInternalKey("c", 2, ValueType::kValue), "c2"},
        {MakeInternalKey("d", 3, ValueType::kDeletion), ""},
    };
    auto view = [&](SequenceNumber snap) {
        return Drain(NewDBIterator(NewVectorIterator(entries, InternalKeyCompare), snap).get());
    };
    CHECK(view(100) == (KV{{"a", "a5"}, {"c", "c7"}}));
    CHECK(view(8) == (KV{{"a", "a5"}, {"b", "b4"}, {"c", "c7"}}));
    CHECK(view(4) == (KV{{"b", "b4"}, {"c", "c2"}}));
    CHECK(view(1) == KV{});
    auto it = NewDBIterator(NewVectorIterator(entries, InternalKeyCompare), 8);
    it->Seek("b");
    CHECK(it->Valid());
    CHECK_EQ(it->key(), std::string_view("b"));
    CHECK_EQ(it->value(), std::string_view("b4"));
}

// M6 — Bloom filter. Notes Part 3 §7.
#include <random>
#include <string>
#include <vector>

#include "helpers.hpp"
#include "lsm/bloom.hpp"
#include "minitest.hpp"

using namespace lsm;

TEST(bloom_no_false_negatives_and_low_fp) {
    std::mt19937_64 rng(2026);
    std::vector<std::string> keys;
    for (int i = 0; i < 20000; ++i) keys.push_back(testutil::RandomBytes(rng, 16));
    std::vector<std::string_view> views(keys.begin(), keys.end());
    std::string filter;
    CreateBloomFilter(views, 10, &filter);
    CHECK_EQ(static_cast<int>(static_cast<unsigned char>(filter.back())), 6);  // k = floor(10*0.69)
    for (auto& k : keys) CHECK(BloomKeyMayMatch(k, filter));
    int fp = 0;
    const int Q = 100000;
    for (int i = 0; i < Q; ++i) fp += BloomKeyMayMatch(testutil::RandomBytes(rng, 16), filter);
    const double rate = 100.0 * fp / Q;
    std::printf("         bloom FP rate at 10 bits/key: %.3f%% (formula: 0.84%%)\n", rate);
    CHECK(rate < 2.0);
}

TEST(bloom_is_deterministic_and_self_describing) {
    std::vector<std::string_view> keys = {"alpha", "beta", "gamma"};
    std::string a, b;
    CreateBloomFilter(keys, 10, &a);
    CreateBloomFilter(keys, 10, &b);
    CHECK_EQ(a, b);                      // same input, same bytes — required for on-disk filters
    CHECK(a.size() >= 8 + 1);            // at least 64 bits + the k byte
    std::string c;
    CreateBloomFilter(keys, 20, &c);     // different k; reader must use the stored k
    for (auto k : keys) CHECK(BloomKeyMayMatch(k, c));
    CHECK(!BloomKeyMayMatch("x", ""));
    CHECK(!BloomKeyMayMatch("x", "\x01"));
    std::string reserved(16, '\0');
    reserved.push_back(static_cast<char>(31));  // k > 30 is reserved => match everything
    CHECK(BloomKeyMayMatch("anything", reserved));
    std::string prefix = "existing-bytes";
    CreateBloomFilter(keys, 10, &prefix);  // must APPEND
    CHECK(prefix.rfind("existing-bytes", 0) == 0);
}

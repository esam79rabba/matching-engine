#include "engine/adaptive_radix_tree.h"

#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace engine {
namespace {

// ─── Test fixture ──────────────────────────────────────────────

class AdaptiveRadixTreeTest : public ::testing::Test {
protected:
    AdaptiveRadixTree<int> tree;
};

// ─── 1. InsertAndFind ──────────────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, InsertAndFind) {
    for (int64_t i = 1; i <= 100; ++i) {
        tree.insert(i * 100) = static_cast<int>(i);
    }
    EXPECT_EQ(tree.size(), 100u);

    for (int64_t i = 1; i <= 100; ++i) {
        int* val = tree.find(i * 100);
        ASSERT_NE(val, nullptr) << "Key " << (i * 100) << " not found";
        EXPECT_EQ(*val, static_cast<int>(i));
    }

    // Non-existent key
    EXPECT_EQ(tree.find(999999), nullptr);
}

// ─── 2. RemoveKey ──────────────────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, RemoveKey) {
    tree.insert(100) = 1;
    tree.insert(200) = 2;
    tree.insert(300) = 3;
    EXPECT_EQ(tree.size(), 3u);

    EXPECT_TRUE(tree.remove(200));
    EXPECT_EQ(tree.size(), 2u);
    EXPECT_EQ(tree.find(200), nullptr);

    // Remaining keys still accessible
    ASSERT_NE(tree.find(100), nullptr);
    EXPECT_EQ(*tree.find(100), 1);
    ASSERT_NE(tree.find(300), nullptr);
    EXPECT_EQ(*tree.find(300), 3);

    // Removing non-existent key returns false
    EXPECT_FALSE(tree.remove(200));
    EXPECT_FALSE(tree.remove(999));
}

// ─── 3. MinMax ─────────────────────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, MinMax) {
    EXPECT_EQ(tree.min(), nullptr);
    EXPECT_EQ(tree.max(), nullptr);

    tree.insert(50) = 50;
    tree.insert(10) = 10;
    tree.insert(90) = 90;
    tree.insert(30) = 30;
    tree.insert(70) = 70;

    ASSERT_NE(tree.min(), nullptr);
    EXPECT_EQ(*tree.min(), 10);

    ASSERT_NE(tree.max(), nullptr);
    EXPECT_EQ(*tree.max(), 90);

    // Remove min, verify new min
    tree.remove(10);
    ASSERT_NE(tree.min(), nullptr);
    EXPECT_EQ(*tree.min(), 30);

    // Remove max, verify new max
    tree.remove(90);
    ASSERT_NE(tree.max(), nullptr);
    EXPECT_EQ(*tree.max(), 70);
}

// ─── 4. AscendingIteration ────────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, AscendingIteration) {
    std::vector<int64_t> keys = {50, 10, 90, 30, 70, 20, 80, 40, 60};
    for (auto k : keys) {
        tree.insert(k) = static_cast<int>(k);
    }

    std::vector<int64_t> result;
    tree.for_each_ascending([&](int64_t key, const int&) {
        result.push_back(key);
    });

    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(result, keys);
}

// ─── 5. DescendingIteration ───────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, DescendingIteration) {
    std::vector<int64_t> keys = {50, 10, 90, 30, 70, 20, 80, 40, 60};
    for (auto k : keys) {
        tree.insert(k) = static_cast<int>(k);
    }

    std::vector<int64_t> result;
    tree.for_each_descending([&](int64_t key, const int&) {
        result.push_back(key);
    });

    std::sort(keys.begin(), keys.end(), std::greater<int64_t>());
    EXPECT_EQ(result, keys);
}

// ─── 6. GrowFromNode4ToNode16 ─────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, GrowFromNode4ToNode16) {
    // Insert > 4 keys that diverge at the same tree level to force Node4 -> Node16 growth
    for (int64_t i = 1; i <= 8; ++i) {
        tree.insert(i) = static_cast<int>(i);
    }
    EXPECT_EQ(tree.size(), 8u);

    // All keys still findable after growth
    for (int64_t i = 1; i <= 8; ++i) {
        int* val = tree.find(i);
        ASSERT_NE(val, nullptr) << "Key " << i << " not found after Node4->Node16 growth";
        EXPECT_EQ(*val, static_cast<int>(i));
    }
}

// ─── 7. GrowToNode48 ──────────────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, GrowToNode48) {
    // Insert > 16 keys to trigger Node16 -> Node48 growth
    for (int64_t i = 1; i <= 32; ++i) {
        tree.insert(i) = static_cast<int>(i);
    }
    EXPECT_EQ(tree.size(), 32u);

    for (int64_t i = 1; i <= 32; ++i) {
        int* val = tree.find(i);
        ASSERT_NE(val, nullptr) << "Key " << i << " not found after Node16->Node48 growth";
        EXPECT_EQ(*val, static_cast<int>(i));
    }
}

// ─── 8. GrowToNode256 ─────────────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, GrowToNode256) {
    // Insert > 48 keys to trigger Node48 -> Node256 growth
    for (int64_t i = 1; i <= 64; ++i) {
        tree.insert(i) = static_cast<int>(i);
    }
    EXPECT_EQ(tree.size(), 64u);

    for (int64_t i = 1; i <= 64; ++i) {
        int* val = tree.find(i);
        ASSERT_NE(val, nullptr) << "Key " << i << " not found after Node48->Node256 growth";
        EXPECT_EQ(*val, static_cast<int>(i));
    }
}

// ─── 9. ShrinkOnRemove ───────────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, ShrinkOnRemove) {
    // Build up to Node16+ size, then remove keys until shrink occurs
    for (int64_t i = 1; i <= 20; ++i) {
        tree.insert(i) = static_cast<int>(i);
    }
    EXPECT_EQ(tree.size(), 20u);

    // Remove down to trigger shrink (Node48->Node16 at <=16, Node16->Node4 at <=4)
    for (int64_t i = 20; i >= 5; --i) {
        EXPECT_TRUE(tree.remove(i));
    }
    EXPECT_EQ(tree.size(), 4u);

    // Remaining keys intact after shrink
    for (int64_t i = 1; i <= 4; ++i) {
        int* val = tree.find(i);
        ASSERT_NE(val, nullptr) << "Key " << i << " not found after shrink";
        EXPECT_EQ(*val, static_cast<int>(i));
    }

    // Continue removing
    for (int64_t i = 4; i >= 1; --i) {
        EXPECT_TRUE(tree.remove(i));
    }
    EXPECT_TRUE(tree.empty());
}

// ─── 10. NegativeKeys ─────────────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, NegativeKeys) {
    tree.insert(-100) = -100;
    tree.insert(-50) = -50;
    tree.insert(0) = 0;
    tree.insert(50) = 50;
    tree.insert(100) = 100;

    EXPECT_EQ(tree.size(), 5u);

    // All keys findable
    ASSERT_NE(tree.find(-100), nullptr);
    EXPECT_EQ(*tree.find(-100), -100);
    ASSERT_NE(tree.find(-50), nullptr);
    EXPECT_EQ(*tree.find(-50), -50);
    ASSERT_NE(tree.find(0), nullptr);
    EXPECT_EQ(*tree.find(0), 0);

    // Min should be -100, max should be 100
    ASSERT_NE(tree.min(), nullptr);
    EXPECT_EQ(*tree.min(), -100);
    ASSERT_NE(tree.max(), nullptr);
    EXPECT_EQ(*tree.max(), 100);

    // Ascending order: -100, -50, 0, 50, 100
    std::vector<int64_t> result;
    tree.for_each_ascending([&](int64_t key, const int&) {
        result.push_back(key);
    });
    std::vector<int64_t> expected = {-100, -50, 0, 50, 100};
    EXPECT_EQ(result, expected);
}

// ─── 11. SparseKeys ───────────────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, SparseKeys) {
    tree.insert(1) = 1;
    tree.insert(100000) = 2;
    tree.insert(99999999) = 3;
    tree.insert(-99999999) = 4;
    tree.insert(INT64_MAX) = 5;
    tree.insert(INT64_MIN) = 6;

    EXPECT_EQ(tree.size(), 6u);

    // All findable
    EXPECT_NE(tree.find(1), nullptr);
    EXPECT_NE(tree.find(100000), nullptr);
    EXPECT_NE(tree.find(99999999), nullptr);
    EXPECT_NE(tree.find(-99999999), nullptr);
    EXPECT_NE(tree.find(INT64_MAX), nullptr);
    EXPECT_NE(tree.find(INT64_MIN), nullptr);

    // Min is INT64_MIN, max is INT64_MAX
    EXPECT_EQ(*tree.min(), 6);
    EXPECT_EQ(*tree.max(), 5);

    // Ascending order correct
    std::vector<int64_t> result;
    tree.for_each_ascending([&](int64_t key, const int&) {
        result.push_back(key);
    });
    std::vector<int64_t> expected = {INT64_MIN, -99999999, 1, 100000, 99999999, INT64_MAX};
    EXPECT_EQ(result, expected);
}

// ─── 12. DuplicateInsert ──────────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, DuplicateInsert) {
    tree.insert(42) = 1;
    EXPECT_EQ(tree.size(), 1u);
    EXPECT_EQ(*tree.find(42), 1);

    // Insert same key again should return existing reference, not create new
    int& ref = tree.insert(42);
    EXPECT_EQ(ref, 1);  // Value unchanged
    EXPECT_EQ(tree.size(), 1u);  // Size unchanged

    // Can update via returned reference
    ref = 99;
    EXPECT_EQ(*tree.find(42), 99);
    EXPECT_EQ(tree.size(), 1u);
}

// ─── Additional: EmptyTree ────────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, EmptyTree) {
    EXPECT_TRUE(tree.empty());
    EXPECT_EQ(tree.size(), 0u);
    EXPECT_EQ(tree.find(0), nullptr);
    EXPECT_EQ(tree.min(), nullptr);
    EXPECT_EQ(tree.max(), nullptr);
    EXPECT_FALSE(tree.remove(0));
}

// ─── Additional: MoveSemantics ────────────────────────────────

TEST_F(AdaptiveRadixTreeTest, MoveSemantics) {
    tree.insert(10) = 10;
    tree.insert(20) = 20;
    EXPECT_EQ(tree.size(), 2u);

    AdaptiveRadixTree<int> moved(std::move(tree));
    EXPECT_EQ(moved.size(), 2u);
    EXPECT_NE(moved.find(10), nullptr);
    EXPECT_NE(moved.find(20), nullptr);
    EXPECT_TRUE(tree.empty());  // NOLINT: testing moved-from state

    AdaptiveRadixTree<int> assigned;
    assigned = std::move(moved);
    EXPECT_EQ(assigned.size(), 2u);
    EXPECT_NE(assigned.find(10), nullptr);
    EXPECT_TRUE(moved.empty());  // NOLINT
}

// ─── PriceLevel integration sanity ────────────────────────────

TEST(AdaptiveRadixTreePriceLevelTest, BasicPriceLevelStorage) {
    // Verify the ART works with the actual PriceLevel type
    AdaptiveRadixTree<int64_t> price_tree;

    // Simulate bid prices
    price_tree.insert(10050) = 10050;
    price_tree.insert(10025) = 10025;
    price_tree.insert(10075) = 10075;

    EXPECT_EQ(price_tree.size(), 3u);

    // Best bid = max price
    ASSERT_NE(price_tree.max(), nullptr);
    EXPECT_EQ(*price_tree.max(), 10075);

    // Best ask = min price (for asks side)
    ASSERT_NE(price_tree.min(), nullptr);
    EXPECT_EQ(*price_tree.min(), 10025);

    // Descending iteration for bids (best first)
    std::vector<int64_t> bid_prices;
    price_tree.for_each_descending([&](int64_t key, const int64_t&) {
        bid_prices.push_back(key);
    });
    std::vector<int64_t> expected_bids = {10075, 10050, 10025};
    EXPECT_EQ(bid_prices, expected_bids);
}

}  // namespace
}  // namespace engine

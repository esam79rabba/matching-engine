#include "gateway/fix_message_store.h"

#include <gtest/gtest.h>
#include <string>

using gateway::FixMessageStore;

TEST(FixMessageStoreTest, StoreAndRetrieve) {
    FixMessageStore store(100);
    store.store("SESSION_A", 1, "8=FIX.4.4" "\x01" "35=A" "\x01" "34=1" "\x01" "10=000" "\x01");

    auto msg = store.get("SESSION_A", 1);
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(*msg, "8=FIX.4.4" "\x01" "35=A" "\x01" "34=1" "\x01" "10=000" "\x01");
}

TEST(FixMessageStoreTest, GetRange) {
    FixMessageStore store(100);

    // Store messages with seq 1..10
    for (uint64_t i = 1; i <= 10; ++i) {
        store.store("SESSION_A", i,
                    std::string("8=FIX.4.4") + "\x01" + "35=D" + "\x01" + "34=" + std::to_string(i) + "\x01" + "10=000" + "\x01");
    }

    // Get range 3..7 — should return 5 messages
    auto range = store.get_range("SESSION_A", 3, 7);
    ASSERT_EQ(range.size(), 5u);

    for (size_t i = 0; i < range.size(); ++i) {
        EXPECT_EQ(range[i].first, 3 + i);
        EXPECT_NE(range[i].second.find("34=" + std::to_string(3 + i)),
                  std::string::npos);
    }
}

TEST(FixMessageStoreTest, MissingMessageReturnsNullopt) {
    FixMessageStore store(100);

    // No messages stored — should return nullopt
    auto msg = store.get("SESSION_A", 1);
    EXPECT_FALSE(msg.has_value());

    // Store one message, request a different seq
    store.store("SESSION_A", 5, "msg5");
    auto msg2 = store.get("SESSION_A", 3);
    EXPECT_FALSE(msg2.has_value());

    // Request from non-existent session
    auto msg3 = store.get("SESSION_B", 5);
    EXPECT_FALSE(msg3.has_value());
}

TEST(FixMessageStoreTest, EvictionRemovesOldest) {
    FixMessageStore store(5);  // max 5 messages per session

    // Store 6 messages — oldest (seq=1) should be evicted
    for (uint64_t i = 1; i <= 6; ++i) {
        store.store("SESSION_A", i, "msg" + std::to_string(i));
    }

    EXPECT_EQ(store.count("SESSION_A"), 5u);

    // seq=1 should be evicted
    EXPECT_FALSE(store.get("SESSION_A", 1).has_value());

    // seq=2..6 should still be present
    for (uint64_t i = 2; i <= 6; ++i) {
        auto msg = store.get("SESSION_A", i);
        ASSERT_TRUE(msg.has_value()) << "Missing seq=" << i;
        EXPECT_EQ(*msg, "msg" + std::to_string(i));
    }
}

TEST(FixMessageStoreTest, ClearSession) {
    FixMessageStore store(100);

    store.store("SESSION_A", 1, "msg1");
    store.store("SESSION_A", 2, "msg2");
    EXPECT_EQ(store.count("SESSION_A"), 2u);

    store.clear("SESSION_A");
    EXPECT_EQ(store.count("SESSION_A"), 0u);
    EXPECT_FALSE(store.get("SESSION_A", 1).has_value());
    EXPECT_FALSE(store.get("SESSION_A", 2).has_value());
}

TEST(FixMessageStoreTest, MultipleSessionsIndependent) {
    FixMessageStore store(100);

    store.store("SESSION_A", 1, "a_msg1");
    store.store("SESSION_A", 2, "a_msg2");
    store.store("SESSION_B", 1, "b_msg1");
    store.store("SESSION_B", 2, "b_msg2");
    store.store("SESSION_B", 3, "b_msg3");

    EXPECT_EQ(store.count("SESSION_A"), 2u);
    EXPECT_EQ(store.count("SESSION_B"), 3u);

    // Verify isolation
    auto a1 = store.get("SESSION_A", 1);
    ASSERT_TRUE(a1.has_value());
    EXPECT_EQ(*a1, "a_msg1");

    auto b1 = store.get("SESSION_B", 1);
    ASSERT_TRUE(b1.has_value());
    EXPECT_EQ(*b1, "b_msg1");

    // Clear one session doesn't affect the other
    store.clear("SESSION_A");
    EXPECT_EQ(store.count("SESSION_A"), 0u);
    EXPECT_EQ(store.count("SESSION_B"), 3u);

    auto b3 = store.get("SESSION_B", 3);
    ASSERT_TRUE(b3.has_value());
    EXPECT_EQ(*b3, "b_msg3");
}

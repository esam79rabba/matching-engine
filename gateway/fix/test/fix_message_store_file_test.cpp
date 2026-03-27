#include "gateway/fix_message_store_file.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <string>

using gateway::FileMessageStore;

namespace {

/// RAII helper to create and clean up a temporary directory for tests.
class TempDir {
public:
    explicit TempDir(const std::string& name) {
        path_ = "/tmp/me_test_" + name + "_" + std::to_string(::getpid());
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::filesystem::remove_all(path_);
    }
    const std::string& path() const { return path_; }
private:
    std::string path_;
};

}  // namespace

TEST(FileMessageStoreTest, StoreAndRetrieve) {
    TempDir dir("fms_basic");
    FileMessageStore store(dir.path(), 100);

    store.store("SESSION_A", 1, "8=FIX.4.4\x01" "35=A\x01" "34=1\x01" "10=000\x01");
    store.store("SESSION_A", 2, "8=FIX.4.4\x01" "35=D\x01" "34=2\x01" "10=000\x01");

    auto msg1 = store.get("SESSION_A", 1);
    ASSERT_TRUE(msg1.has_value());
    EXPECT_EQ(*msg1, "8=FIX.4.4\x01" "35=A\x01" "34=1\x01" "10=000\x01");

    auto msg2 = store.get("SESSION_A", 2);
    ASSERT_TRUE(msg2.has_value());
    EXPECT_EQ(*msg2, "8=FIX.4.4\x01" "35=D\x01" "34=2\x01" "10=000\x01");

    EXPECT_EQ(store.count("SESSION_A"), 2u);

    // Verify the file was created
    auto file_path = dir.path() + "/SESSION_A.msgstore";
    EXPECT_TRUE(std::filesystem::exists(file_path));
}

TEST(FileMessageStoreTest, PersistsAcrossRestart) {
    TempDir dir("fms_persist");

    // Phase 1: store messages
    {
        FileMessageStore store(dir.path(), 100);
        store.store("PILOT_ALPHA", 1, "msg_one");
        store.store("PILOT_ALPHA", 2, "msg_two");
        store.store("PILOT_ALPHA", 3, "msg_three");
    }  // store destroyed — simulates process exit

    // Phase 2: create new store on same directory, load from file
    {
        FileMessageStore store(dir.path(), 100);
        uint32_t loaded = store.load();
        EXPECT_EQ(loaded, 3u);

        EXPECT_EQ(store.count("PILOT_ALPHA"), 3u);

        auto msg1 = store.get("PILOT_ALPHA", 1);
        ASSERT_TRUE(msg1.has_value());
        EXPECT_EQ(*msg1, "msg_one");

        auto msg2 = store.get("PILOT_ALPHA", 2);
        ASSERT_TRUE(msg2.has_value());
        EXPECT_EQ(*msg2, "msg_two");

        auto msg3 = store.get("PILOT_ALPHA", 3);
        ASSERT_TRUE(msg3.has_value());
        EXPECT_EQ(*msg3, "msg_three");
    }
}

TEST(FileMessageStoreTest, MultipleSessionsIndependent) {
    TempDir dir("fms_multi");

    {
        FileMessageStore store(dir.path(), 100);
        store.store("SESSION_A", 1, "a_msg1");
        store.store("SESSION_A", 2, "a_msg2");
        store.store("SESSION_B", 1, "b_msg1");
        store.store("SESSION_B", 2, "b_msg2");
        store.store("SESSION_B", 3, "b_msg3");
    }

    // Verify separate files
    EXPECT_TRUE(std::filesystem::exists(dir.path() + "/SESSION_A.msgstore"));
    EXPECT_TRUE(std::filesystem::exists(dir.path() + "/SESSION_B.msgstore"));

    // Reload and verify
    FileMessageStore store2(dir.path(), 100);
    uint32_t loaded = store2.load();
    EXPECT_EQ(loaded, 5u);

    EXPECT_EQ(store2.count("SESSION_A"), 2u);
    EXPECT_EQ(store2.count("SESSION_B"), 3u);

    auto a1 = store2.get("SESSION_A", 1);
    ASSERT_TRUE(a1.has_value());
    EXPECT_EQ(*a1, "a_msg1");

    auto b3 = store2.get("SESSION_B", 3);
    ASSERT_TRUE(b3.has_value());
    EXPECT_EQ(*b3, "b_msg3");
}

TEST(FileMessageStoreTest, LargeMessageCount) {
    TempDir dir("fms_large");

    const uint32_t n = 1000;

    // Store 1000 messages
    {
        FileMessageStore store(dir.path(), n + 100);  // large enough to hold all
        for (uint32_t i = 1; i <= n; ++i) {
            store.store("STRESS_SESSION", i,
                        "8=FIX.4.4\x01" "35=D\x01" "34=" +
                        std::to_string(i) + "\x01" "10=000\x01");
        }
        EXPECT_EQ(store.count("STRESS_SESSION"), n);
    }

    // Reload and verify all present
    {
        FileMessageStore store(dir.path(), n + 100);
        uint32_t loaded = store.load();
        EXPECT_EQ(loaded, n);
        EXPECT_EQ(store.count("STRESS_SESSION"), n);

        // Spot-check a few
        auto first = store.get("STRESS_SESSION", 1);
        ASSERT_TRUE(first.has_value());
        EXPECT_NE(first->find("34=1"), std::string::npos);

        auto last = store.get("STRESS_SESSION", n);
        ASSERT_TRUE(last.has_value());
        EXPECT_NE(last->find("34=" + std::to_string(n)), std::string::npos);

        auto mid = store.get("STRESS_SESSION", 500);
        ASSERT_TRUE(mid.has_value());
        EXPECT_NE(mid->find("34=500"), std::string::npos);
    }
}

TEST(FileMessageStoreTest, GetRangeAfterLoad) {
    TempDir dir("fms_range");

    {
        FileMessageStore store(dir.path(), 100);
        for (uint64_t i = 1; i <= 10; ++i) {
            store.store("RANGE_SESSION", i, "msg_" + std::to_string(i));
        }
    }

    FileMessageStore store2(dir.path(), 100);
    store2.load();

    auto range = store2.get_range("RANGE_SESSION", 3, 7);
    ASSERT_EQ(range.size(), 5u);
    EXPECT_EQ(range[0].first, 3u);
    EXPECT_EQ(range[0].second, "msg_3");
    EXPECT_EQ(range[4].first, 7u);
    EXPECT_EQ(range[4].second, "msg_7");
}

TEST(FileMessageStoreTest, LoadFromEmptyDirectory) {
    TempDir dir("fms_empty");
    FileMessageStore store(dir.path(), 100);
    uint32_t loaded = store.load();
    EXPECT_EQ(loaded, 0u);
}

TEST(FileMessageStoreTest, ClearDoesNotDeleteFile) {
    TempDir dir("fms_clear");

    FileMessageStore store(dir.path(), 100);
    store.store("CLEAR_SESSION", 1, "msg1");
    store.store("CLEAR_SESSION", 2, "msg2");
    EXPECT_EQ(store.count("CLEAR_SESSION"), 2u);

    store.clear("CLEAR_SESSION");
    EXPECT_EQ(store.count("CLEAR_SESSION"), 0u);

    // File still exists
    EXPECT_TRUE(std::filesystem::exists(dir.path() + "/CLEAR_SESSION.msgstore"));
}

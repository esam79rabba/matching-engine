#include "gateway/fix_session_store.h"

#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>
#include <cstdio>

namespace {

using namespace gateway;

/// Helper: unique temp file path per test.
static std::string temp_path(const std::string& name) {
    return std::filesystem::temp_directory_path().string() + "/fix_session_store_test_" + name + ".dat";
}

/// Cleanup helper.
static void remove_file(const std::string& path) {
    std::filesystem::remove(path);
    std::filesystem::remove(path + ".tmp");
}

// ─── SaveAndLoad ─────────────────────────────────────────────────

TEST(FixSessionStoreTest, SaveAndLoad) {
    auto path = temp_path("save_load");
    remove_file(path);

    // Create store, add two sessions, save.
    {
        FixSessionStore store(path);

        PersistedSessionState s1;
        s1.session_id = 1;
        s1.sender_comp_id = "MM_ALPHA";
        s1.target_comp_id = "VENUE";
        s1.inbound_seq = 10;
        s1.outbound_seq = 20;
        store.upsert(s1);

        PersistedSessionState s2;
        s2.session_id = 2;
        s2.sender_comp_id = "BRK_BETA";
        s2.target_comp_id = "VENUE";
        s2.inbound_seq = 5;
        s2.outbound_seq = 15;
        store.upsert(s2);

        ASSERT_TRUE(store.save());
    }

    // Create new store on same path, load, verify.
    {
        FixSessionStore store(path);
        ASSERT_TRUE(store.load());
        EXPECT_EQ(store.sessions().size(), 2u);

        auto s1 = store.get("MM_ALPHA");
        ASSERT_TRUE(s1.has_value());
        EXPECT_EQ(s1->session_id, 1u);
        EXPECT_EQ(s1->target_comp_id, "VENUE");
        EXPECT_EQ(s1->inbound_seq, 10u);
        EXPECT_EQ(s1->outbound_seq, 20u);

        auto s2 = store.get("BRK_BETA");
        ASSERT_TRUE(s2.has_value());
        EXPECT_EQ(s2->session_id, 2u);
        EXPECT_EQ(s2->inbound_seq, 5u);
        EXPECT_EQ(s2->outbound_seq, 15u);
    }

    remove_file(path);
}

// ─── MissingFileLoadsCleanly ─────────────────────────────────────

TEST(FixSessionStoreTest, MissingFileLoadsCleanly) {
    auto path = temp_path("missing_file");
    remove_file(path);

    FixSessionStore store(path);
    EXPECT_TRUE(store.load());
    EXPECT_TRUE(store.sessions().empty());

    remove_file(path);
}

// ─── CorruptLineSkipped ──────────────────────────────────────────

TEST(FixSessionStoreTest, CorruptLineSkipped) {
    auto path = temp_path("corrupt_line");
    remove_file(path);

    // Write a file with one good line and one bad line.
    {
        std::ofstream out(path);
        out << "1|MM_ALPHA|VENUE|10|20\n";
        out << "THIS_IS_CORRUPT\n";
        out << "2|BRK_BETA|VENUE|5|15\n";
    }

    FixSessionStore store(path);
    ASSERT_TRUE(store.load());
    EXPECT_EQ(store.sessions().size(), 2u);

    auto s1 = store.get("MM_ALPHA");
    ASSERT_TRUE(s1.has_value());
    EXPECT_EQ(s1->session_id, 1u);

    auto s2 = store.get("BRK_BETA");
    ASSERT_TRUE(s2.has_value());
    EXPECT_EQ(s2->session_id, 2u);

    remove_file(path);
}

// ─── UpsertUpdatesExisting ───────────────────────────────────────

TEST(FixSessionStoreTest, UpsertUpdatesExisting) {
    auto path = temp_path("upsert");
    remove_file(path);

    FixSessionStore store(path);

    PersistedSessionState s1;
    s1.session_id = 1;
    s1.sender_comp_id = "MM_ALPHA";
    s1.target_comp_id = "VENUE";
    s1.inbound_seq = 10;
    s1.outbound_seq = 20;
    store.upsert(s1);

    // Update sequences.
    s1.inbound_seq = 100;
    s1.outbound_seq = 200;
    store.upsert(s1);

    auto result = store.get("MM_ALPHA");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->inbound_seq, 100u);
    EXPECT_EQ(result->outbound_seq, 200u);
    EXPECT_EQ(store.sessions().size(), 1u);

    remove_file(path);
}

// ─── RemoveSession ───────────────────────────────────────────────

TEST(FixSessionStoreTest, RemoveSession) {
    auto path = temp_path("remove");
    remove_file(path);

    FixSessionStore store(path);

    PersistedSessionState s1;
    s1.session_id = 1;
    s1.sender_comp_id = "MM_ALPHA";
    s1.target_comp_id = "VENUE";
    s1.inbound_seq = 10;
    s1.outbound_seq = 20;
    store.upsert(s1);

    EXPECT_TRUE(store.get("MM_ALPHA").has_value());

    store.remove("MM_ALPHA");
    EXPECT_FALSE(store.get("MM_ALPHA").has_value());
    EXPECT_TRUE(store.sessions().empty());

    remove_file(path);
}

// ─── AtomicSaveDoesNotCorrupt ────────────────────────────────────

TEST(FixSessionStoreTest, AtomicSaveDoesNotCorrupt) {
    auto path = temp_path("atomic_save");
    remove_file(path);

    // Save sessions.
    {
        FixSessionStore store(path);

        PersistedSessionState s1;
        s1.session_id = 1;
        s1.sender_comp_id = "MM_ALPHA";
        s1.target_comp_id = "VENUE";
        s1.inbound_seq = 42;
        s1.outbound_seq = 84;
        store.upsert(s1);

        ASSERT_TRUE(store.save());
    }

    // Verify file exists and is readable.
    EXPECT_TRUE(std::filesystem::exists(path));

    // Verify no temp file left behind.
    EXPECT_FALSE(std::filesystem::exists(path + ".tmp"));

    // Reload and verify integrity.
    {
        FixSessionStore store(path);
        ASSERT_TRUE(store.load());
        EXPECT_EQ(store.sessions().size(), 1u);
        auto s = store.get("MM_ALPHA");
        ASSERT_TRUE(s.has_value());
        EXPECT_EQ(s->inbound_seq, 42u);
        EXPECT_EQ(s->outbound_seq, 84u);
    }

    remove_file(path);
}

}  // namespace

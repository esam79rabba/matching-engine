/// Segmented journal tests — rotation, multi-segment replay, integrity.
#include "engine/journal_segmented.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <cstring>

namespace engine {
namespace {

const std::string TEST_DIR = "/tmp/matching_engine_seg_journal_test";

class SegmentedJournalTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(TEST_DIR);
    }
    void TearDown() override {
        std::filesystem::remove_all(TEST_DIR);
    }

    static JournalEntry make_entry(SequenceNum seq, const void* payload, uint32_t size) {
        JournalEntry e{};
        e.sequence = seq;
        e.timestamp = seq * 1000;
        e.partition_id = 1;
        e.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
        e.payload_size = size;
        e.payload = payload;
        return e;
    }
};

TEST_F(SegmentedJournalTest, OpenCreatesDirectory) {
    SegmentedJournal::Config cfg;
    cfg.base_path = TEST_DIR;
    cfg.flush_mode = FileJournal::FlushMode::NONE;

    SegmentedJournal journal(cfg);
    ASSERT_TRUE(journal.open());
    EXPECT_TRUE(journal.is_open());
    EXPECT_EQ(journal.entry_count(), 0u);
    EXPECT_EQ(journal.segment_count(), 0u);
    journal.close();

    EXPECT_TRUE(std::filesystem::exists(TEST_DIR));
}

TEST_F(SegmentedJournalTest, AppendAndReplay) {
    SegmentedJournal::Config cfg;
    cfg.base_path = TEST_DIR;
    cfg.flush_mode = FileJournal::FlushMode::SYNC;

    SegmentedJournal journal(cfg);
    ASSERT_TRUE(journal.open());

    for (SequenceNum i = 1; i <= 10; ++i) {
        uint64_t payload = i * 100;
        auto entry = make_entry(i, &payload, sizeof(payload));
        EXPECT_EQ(journal.append(entry), JournalResult::OK);
    }

    EXPECT_EQ(journal.entry_count(), 10u);
    EXPECT_EQ(journal.last_sequence(), 10u);

    // Replay all
    struct Ctx { uint64_t count = 0; SequenceNum first = 0; SequenceNum last = 0; };
    Ctx ctx;
    journal.replay_from(1, [](const JournalEntry& e, void* c) {
        auto* x = static_cast<Ctx*>(c);
        if (x->count == 0) x->first = e.sequence;
        x->last = e.sequence;
        ++x->count;
    }, &ctx);

    EXPECT_EQ(ctx.count, 10u);
    EXPECT_EQ(ctx.first, 1u);
    EXPECT_EQ(ctx.last, 10u);

    journal.close();
}

TEST_F(SegmentedJournalTest, SegmentRotation) {
    SegmentedJournal::Config cfg;
    cfg.base_path = TEST_DIR;
    cfg.max_segment_bytes = 256;  // Very small — forces rotation quickly
    cfg.flush_mode = FileJournal::FlushMode::SYNC;

    SegmentedJournal journal(cfg);
    ASSERT_TRUE(journal.open());

    // Write enough entries to force multiple rotations
    // Each entry: 24-byte header + 8-byte payload + 4-byte CRC = 36 bytes
    // With 256-byte limit, ~7 entries per segment
    for (SequenceNum i = 1; i <= 30; ++i) {
        uint64_t payload = i * 100;
        auto entry = make_entry(i, &payload, sizeof(payload));
        EXPECT_EQ(journal.append(entry), JournalResult::OK);
    }

    EXPECT_EQ(journal.entry_count(), 30u);
    EXPECT_GT(journal.segment_count(), 0u);  // At least one rotation occurred

    // Replay across segments
    struct Ctx { uint64_t count = 0; };
    Ctx ctx;
    journal.replay_from(1, [](const JournalEntry&, void* c) {
        ++static_cast<Ctx*>(c)->count;
    }, &ctx);

    EXPECT_EQ(ctx.count, 30u);

    journal.close();
}

TEST_F(SegmentedJournalTest, ReplayFromMidSegment) {
    SegmentedJournal::Config cfg;
    cfg.base_path = TEST_DIR;
    cfg.max_segment_bytes = 256;
    cfg.flush_mode = FileJournal::FlushMode::SYNC;

    SegmentedJournal journal(cfg);
    ASSERT_TRUE(journal.open());

    for (SequenceNum i = 1; i <= 20; ++i) {
        uint64_t payload = i;
        auto entry = make_entry(i, &payload, sizeof(payload));
        (void)journal.append(entry);
    }

    // Replay from sequence 15
    struct Ctx { uint64_t count = 0; SequenceNum first = 0; };
    Ctx ctx;
    journal.replay_from(15, [](const JournalEntry& e, void* c) {
        auto* x = static_cast<Ctx*>(c);
        if (x->count == 0) x->first = e.sequence;
        ++x->count;
    }, &ctx);

    EXPECT_EQ(ctx.count, 6u);  // 15, 16, 17, 18, 19, 20
    EXPECT_EQ(ctx.first, 15u);

    journal.close();
}

TEST_F(SegmentedJournalTest, ReopenRecoversPriorSegments) {
    {
        SegmentedJournal::Config cfg;
        cfg.base_path = TEST_DIR;
        cfg.max_segment_bytes = 256;
        cfg.flush_mode = FileJournal::FlushMode::SYNC;

        SegmentedJournal journal(cfg);
        ASSERT_TRUE(journal.open());

        for (SequenceNum i = 1; i <= 20; ++i) {
            uint64_t payload = i;
            auto entry = make_entry(i, &payload, sizeof(payload));
            (void)journal.append(entry);
        }
        journal.close();
    }

    // Reopen and verify recovery
    {
        SegmentedJournal::Config cfg;
        cfg.base_path = TEST_DIR;
        cfg.max_segment_bytes = 256;
        cfg.flush_mode = FileJournal::FlushMode::NONE;

        SegmentedJournal journal(cfg);
        ASSERT_TRUE(journal.open());

        // Replay all entries
        struct Ctx { uint64_t count = 0; };
        Ctx ctx;
        journal.replay_from(1, [](const JournalEntry&, void* c) {
            ++static_cast<Ctx*>(c)->count;
        }, &ctx);

        EXPECT_EQ(ctx.count, 20u);
        journal.close();
    }
}

TEST_F(SegmentedJournalTest, IntegrityVerification) {
    SegmentedJournal::Config cfg;
    cfg.base_path = TEST_DIR;
    cfg.max_segment_bytes = 256;
    cfg.flush_mode = FileJournal::FlushMode::SYNC;

    SegmentedJournal journal(cfg);
    ASSERT_TRUE(journal.open());

    for (SequenceNum i = 1; i <= 15; ++i) {
        uint64_t payload = i * 10;
        auto entry = make_entry(i, &payload, sizeof(payload));
        (void)journal.append(entry);
    }

    EXPECT_EQ(journal.verify_integrity(), 15u);
    journal.close();
}

TEST_F(SegmentedJournalTest, EmptyJournalOperations) {
    SegmentedJournal::Config cfg;
    cfg.base_path = TEST_DIR;
    cfg.flush_mode = FileJournal::FlushMode::NONE;

    SegmentedJournal journal(cfg);
    ASSERT_TRUE(journal.open());

    EXPECT_EQ(journal.entry_count(), 0u);
    EXPECT_EQ(journal.last_sequence(), 0u);
    EXPECT_EQ(journal.verify_integrity(), 0u);

    struct Ctx { uint64_t count = 0; };
    Ctx ctx;
    journal.replay_from(1, [](const JournalEntry&, void* c) {
        ++static_cast<Ctx*>(c)->count;
    }, &ctx);
    EXPECT_EQ(ctx.count, 0u);

    journal.close();
}

}  // namespace
}  // namespace engine

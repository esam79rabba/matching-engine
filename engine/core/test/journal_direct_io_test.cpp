#include "engine/journal_direct_io.h"
#include "engine/journal_file.h"
#include <gtest/gtest.h>
#include <cstring>
#include <filesystem>

namespace engine {
namespace {

const std::string TEST_BASE = "/tmp/matching_engine_direct_io_test";

class DirectIOJournalTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(TEST_BASE);
    }
    void TearDown() override {
        std::filesystem::remove_all(TEST_BASE);
    }

    static JournalEntry make_entry(SequenceNum seq, uint32_t payload_val) {
        JournalEntry entry{};
        entry.sequence = seq;
        entry.timestamp = seq * 1000;
        entry.partition_id = 1;
        entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(payload_val);
        entry.payload = nullptr;  // caller must set
        return entry;
    }
};

TEST_F(DirectIOJournalTest, OpenCreatesDirectory) {
    DirectIOJournal::Config cfg;
    cfg.base_path = TEST_BASE;
    cfg.alignment = 4096;

    DirectIOJournal journal(cfg);
    ASSERT_TRUE(journal.open());
    EXPECT_TRUE(journal.is_open());
    EXPECT_TRUE(std::filesystem::exists(TEST_BASE));
    EXPECT_EQ(journal.entry_count(), 0u);
    EXPECT_EQ(journal.last_sequence(), 0u);
    journal.close();
}

TEST_F(DirectIOJournalTest, AppendAndReplay) {
    DirectIOJournal::Config cfg;
    cfg.base_path = TEST_BASE;
    cfg.alignment = 4096;

    DirectIOJournal journal(cfg);
    ASSERT_TRUE(journal.open());

    // Write 10 entries
    for (SequenceNum i = 1; i <= 10; ++i) {
        uint32_t payload = static_cast<uint32_t>(i * 100);
        JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(payload);
        entry.payload = &payload;
        EXPECT_EQ(journal.append(entry), JournalResult::OK);
    }

    EXPECT_EQ(journal.entry_count(), 10u);
    EXPECT_EQ(journal.last_sequence(), 10u);

    // Replay all
    struct Ctx { uint64_t count = 0; SequenceNum first = 0; SequenceNum last = 0; };
    Ctx ctx;
    auto visitor = [](const JournalEntry& e, void* c) {
        auto* x = static_cast<Ctx*>(c);
        if (x->count == 0) x->first = e.sequence;
        x->last = e.sequence;
        ++x->count;
    };

    uint64_t visited = journal.replay_from(1, visitor, &ctx);
    EXPECT_EQ(visited, 10u);
    EXPECT_EQ(ctx.count, 10u);
    EXPECT_EQ(ctx.first, 1u);
    EXPECT_EQ(ctx.last, 10u);

    journal.close();
}

TEST_F(DirectIOJournalTest, SegmentRotation) {
    DirectIOJournal::Config cfg;
    cfg.base_path = TEST_BASE;
    cfg.max_segment_bytes = 256;  // Very small — force rotation
    cfg.alignment = 512;          // Smaller alignment for test

    DirectIOJournal journal(cfg);
    ASSERT_TRUE(journal.open());

    // Write enough entries to trigger multiple rotations
    for (SequenceNum i = 1; i <= 20; ++i) {
        uint64_t payload = i * 1000;
        JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(payload);
        entry.payload = &payload;
        EXPECT_EQ(journal.append(entry), JournalResult::OK);
    }

    EXPECT_EQ(journal.entry_count(), 20u);
    EXPECT_GT(journal.segment_count(), 1u) << "Should have multiple segments with tiny max_segment_bytes";

    // Replay all and verify
    struct Ctx { uint64_t count = 0; };
    Ctx ctx;
    auto visitor = [](const JournalEntry&, void* c) { ++static_cast<Ctx*>(c)->count; };
    uint64_t visited = journal.replay_from(1, visitor, &ctx);
    EXPECT_EQ(visited, 20u);

    journal.close();
}

TEST_F(DirectIOJournalTest, ReplayFromMidpoint) {
    DirectIOJournal::Config cfg;
    cfg.base_path = TEST_BASE;
    cfg.alignment = 4096;

    DirectIOJournal journal(cfg);
    ASSERT_TRUE(journal.open());

    for (SequenceNum i = 1; i <= 10; ++i) {
        uint32_t payload = static_cast<uint32_t>(i);
        JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(payload);
        entry.payload = &payload;
        journal.append(entry);
    }

    // Replay from sequence 5
    struct Ctx { uint64_t count = 0; SequenceNum first = 0; };
    Ctx ctx;
    auto visitor = [](const JournalEntry& e, void* c) {
        auto* x = static_cast<Ctx*>(c);
        if (x->count == 0) x->first = e.sequence;
        ++x->count;
    };

    uint64_t visited = journal.replay_from(5, visitor, &ctx);
    EXPECT_EQ(visited, 6u);  // sequences 5,6,7,8,9,10
    EXPECT_EQ(ctx.first, 5u);

    journal.close();
}

TEST_F(DirectIOJournalTest, IntegrityVerification) {
    DirectIOJournal::Config cfg;
    cfg.base_path = TEST_BASE;
    cfg.alignment = 4096;

    DirectIOJournal journal(cfg);
    ASSERT_TRUE(journal.open());

    for (SequenceNum i = 1; i <= 7; ++i) {
        uint32_t payload = static_cast<uint32_t>(i * 10);
        JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(payload);
        entry.payload = &payload;
        journal.append(entry);
    }

    EXPECT_EQ(journal.verify_integrity(), 7u);
    journal.close();
}

TEST_F(DirectIOJournalTest, ReopenRecoversPriorSegments) {
    DirectIOJournal::Config cfg;
    cfg.base_path = TEST_BASE;
    cfg.max_segment_bytes = 256;  // Small to force rotation
    cfg.alignment = 512;

    // Write entries and close
    {
        DirectIOJournal journal(cfg);
        ASSERT_TRUE(journal.open());

        for (SequenceNum i = 1; i <= 15; ++i) {
            uint64_t payload = i * 100;
            JournalEntry entry{};
            entry.sequence = i;
            entry.timestamp = i * 1000;
            entry.partition_id = 1;
            entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
            entry.payload_size = sizeof(payload);
            entry.payload = &payload;
            EXPECT_EQ(journal.append(entry), JournalResult::OK);
        }
        journal.close();
    }

    // Reopen and verify all entries are recoverable
    {
        DirectIOJournal journal(cfg);
        ASSERT_TRUE(journal.open());

        struct Ctx { uint64_t count = 0; SequenceNum last = 0; };
        Ctx ctx;
        auto visitor = [](const JournalEntry& e, void* c) {
            auto* x = static_cast<Ctx*>(c);
            x->last = e.sequence;
            ++x->count;
        };

        uint64_t visited = journal.replay_from(1, visitor, &ctx);
        EXPECT_EQ(visited, 15u);
        EXPECT_EQ(ctx.last, 15u);
        journal.close();
    }
}

TEST_F(DirectIOJournalTest, CompatibleWithFileJournal) {
    // Write with DirectIOJournal, then read with FileJournal.
    // Both use the same record format: [RecordHeader][Payload][CRC32].
    //
    // Note: DirectIOJournal pads writes to alignment boundaries,
    // so there may be zero-padding bytes after the record data.
    // FileJournal reads sequentially and will stop at garbage/padding.
    // This test verifies at least the first record is readable.

    DirectIOJournal::Config dio_cfg;
    dio_cfg.base_path = TEST_BASE;
    dio_cfg.alignment = 4096;

    // Write a single entry with DirectIOJournal
    {
        DirectIOJournal journal(dio_cfg);
        ASSERT_TRUE(journal.open());

        uint32_t payload = 42;
        JournalEntry entry{};
        entry.sequence = 1;
        entry.timestamp = 1000000;
        entry.partition_id = 1;
        entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(payload);
        entry.payload = &payload;
        EXPECT_EQ(journal.append(entry), JournalResult::OK);
        journal.close();
    }

    // Find the segment file
    std::string segment_file;
    for (const auto& entry : std::filesystem::directory_iterator(TEST_BASE)) {
        if (entry.path().extension() == ".journal") {
            segment_file = entry.path().string();
            break;
        }
    }
    ASSERT_FALSE(segment_file.empty()) << "No segment file found";

    // Read with FileJournal
    FileJournal fj;
    ASSERT_TRUE(fj.open(segment_file, FileJournal::FlushMode::NONE));

    // The file should have at least 1 valid record
    EXPECT_GE(fj.verify_integrity(), 1u)
        << "FileJournal should read records written by DirectIOJournal";

    // Replay and verify payload
    struct Ctx { uint64_t count = 0; uint32_t payload = 0; };
    Ctx ctx;
    auto visitor = [](const JournalEntry& e, void* c) {
        auto* x = static_cast<Ctx*>(c);
        if (e.payload_size == sizeof(uint32_t) && e.payload) {
            std::memcpy(&x->payload, e.payload, sizeof(uint32_t));
        }
        ++x->count;
    };

    uint64_t visited = fj.replay_from(1, visitor, &ctx);
    EXPECT_GE(visited, 1u);
    EXPECT_EQ(ctx.payload, 42u);

    fj.close();
}

}  // namespace
}  // namespace engine

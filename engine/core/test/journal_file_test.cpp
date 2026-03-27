#include "engine/journal_file.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace engine {
namespace {

const std::string TEST_PATH = "/tmp/matching_engine_journal_test.bin";

class FileJournalTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::remove(TEST_PATH.c_str());
    }
    void TearDown() override {
        std::remove(TEST_PATH.c_str());
    }
};

TEST_F(FileJournalTest, OpenCreatesFile) {
    FileJournal journal;
    ASSERT_TRUE(journal.open(TEST_PATH, FileJournal::FlushMode::NONE));
    EXPECT_TRUE(journal.is_open());
    EXPECT_EQ(journal.entry_count(), 0u);
    journal.close();
}

TEST_F(FileJournalTest, AppendAndReadBack) {
    {
        FileJournal journal;
        ASSERT_TRUE(journal.open(TEST_PATH, FileJournal::FlushMode::SYNC));

        uint32_t payload = 42;
        JournalEntry entry{};
        entry.sequence = 1;
        entry.timestamp = 1000000;
        entry.partition_id = 1;
        entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(payload);
        entry.payload = &payload;

        auto result = journal.append(entry);
        EXPECT_EQ(result, JournalResult::OK);
        EXPECT_EQ(journal.entry_count(), 1u);
        EXPECT_EQ(journal.last_sequence(), 1u);
        journal.close();
    }

    // Reopen and verify
    {
        FileJournal journal;
        ASSERT_TRUE(journal.open(TEST_PATH, FileJournal::FlushMode::NONE));
        EXPECT_EQ(journal.entry_count(), 1u);
        EXPECT_EQ(journal.last_sequence(), 1u);
        journal.close();
    }
}

TEST_F(FileJournalTest, MultipleAppends) {
    FileJournal journal;
    ASSERT_TRUE(journal.open(TEST_PATH, FileJournal::FlushMode::NONE));

    for (SequenceNum i = 1; i <= 10; ++i) {
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

    EXPECT_EQ(journal.entry_count(), 10u);
    EXPECT_EQ(journal.last_sequence(), 10u);
    journal.close();
}

TEST_F(FileJournalTest, ReplayFromSequence) {
    FileJournal journal;
    ASSERT_TRUE(journal.open(TEST_PATH, FileJournal::FlushMode::SYNC));

    for (SequenceNum i = 1; i <= 5; ++i) {
        uint8_t payload = static_cast<uint8_t>(i);
        JournalEntry entry{};
        entry.sequence = i;
        entry.timestamp = i * 1000;
        entry.partition_id = 1;
        entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
        entry.payload_size = sizeof(payload);
        entry.payload = &payload;
        journal.append(entry);
    }

    // Replay from sequence 3
    struct Ctx { uint64_t count = 0; SequenceNum first = 0; };
    Ctx ctx;
    auto visitor = [](const JournalEntry& e, void* c) {
        auto* x = static_cast<Ctx*>(c);
        if (x->count == 0) x->first = e.sequence;
        ++x->count;
    };

    uint64_t visited = journal.replay_from(3, visitor, &ctx);
    EXPECT_EQ(visited, 3u);
    EXPECT_EQ(ctx.count, 3u);
    EXPECT_EQ(ctx.first, 3u);

    journal.close();
}

TEST_F(FileJournalTest, IntegrityVerification) {
    {
        FileJournal journal;
        ASSERT_TRUE(journal.open(TEST_PATH, FileJournal::FlushMode::SYNC));

        for (SequenceNum i = 1; i <= 5; ++i) {
            uint32_t payload = static_cast<uint32_t>(i * 10);
            JournalEntry entry{};
            entry.sequence = i; entry.timestamp = i; entry.partition_id = 1;
            entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
            entry.payload_size = sizeof(payload); entry.payload = &payload;
            journal.append(entry);
        }
        journal.close();
    }

    // Verify
    FileJournal journal;
    ASSERT_TRUE(journal.open(TEST_PATH, FileJournal::FlushMode::NONE));
    EXPECT_EQ(journal.verify_integrity(), 5u);
    journal.close();
}

TEST_F(FileJournalTest, CorruptedRecordStopsReplay) {
    {
        FileJournal journal;
        ASSERT_TRUE(journal.open(TEST_PATH, FileJournal::FlushMode::SYNC));

        for (SequenceNum i = 1; i <= 3; ++i) {
            uint32_t payload = static_cast<uint32_t>(i);
            JournalEntry entry{};
            entry.sequence = i; entry.timestamp = i; entry.partition_id = 1;
            entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
            entry.payload_size = sizeof(payload); entry.payload = &payload;
            journal.append(entry);
        }
        journal.close();
    }

    // Corrupt the file by modifying a byte in the middle
    {
        FILE* f = fopen(TEST_PATH.c_str(), "r+b");
        ASSERT_NE(f, nullptr);
        // Seek to somewhere in the second record and flip a byte
        fseek(f, 40, SEEK_SET);  // Past first record
        uint8_t byte = 0xFF;
        fwrite(&byte, 1, 1, f);
        fclose(f);
    }

    FileJournal journal;
    ASSERT_TRUE(journal.open(TEST_PATH, FileJournal::FlushMode::NONE));
    // Verify stops at corruption point
    uint64_t valid = journal.verify_integrity();
    EXPECT_LT(valid, 3u);  // Should find fewer than 3 valid records
    journal.close();
}

TEST_F(FileJournalTest, EmptyPayloadAppend) {
    FileJournal journal;
    ASSERT_TRUE(journal.open(TEST_PATH, FileJournal::FlushMode::SYNC));

    JournalEntry entry{};
    entry.sequence = 1;
    entry.timestamp = 1000;
    entry.partition_id = 1;
    entry.entry_type = JournalEntry::TYPE_SNAPSHOT_MARKER;
    entry.payload_size = 0;
    entry.payload = nullptr;

    EXPECT_EQ(journal.append(entry), JournalResult::OK);
    EXPECT_EQ(journal.entry_count(), 1u);

    // Replay should work
    uint64_t count = 0;
    journal.replay_from(1, [](const JournalEntry&, void* c) { ++(*static_cast<uint64_t*>(c)); }, &count);
    EXPECT_EQ(count, 1u);

    journal.close();
}

}  // namespace
}  // namespace engine

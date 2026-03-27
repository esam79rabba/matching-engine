#include "engine/journal_stub.h"
#include <gtest/gtest.h>
#include <cstring>

namespace engine {
namespace {

TEST(JournalStubTest, InitialStateIsEmpty) {
    JournalStub journal(100);
    EXPECT_EQ(journal.entry_count(), 0u);
    EXPECT_EQ(journal.last_sequence(), 0u);
}

TEST(JournalStubTest, AppendStoresEntry) {
    JournalStub journal(100);

    uint32_t payload_data = 42;
    JournalEntry entry{};
    entry.sequence = 1;
    entry.timestamp = 1000000;
    entry.partition_id = 1;
    entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
    entry.payload_size = sizeof(payload_data);
    entry.payload = &payload_data;

    auto result = journal.append(entry);
    EXPECT_EQ(result, JournalResult::OK);
    EXPECT_EQ(journal.entry_count(), 1u);
    EXPECT_EQ(journal.last_sequence(), 1u);
}

TEST(JournalStubTest, AppendCopiesPayload) {
    JournalStub journal(100);

    uint8_t buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    JournalEntry entry{};
    entry.sequence = 1;
    entry.partition_id = 1;
    entry.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
    entry.payload_size = sizeof(buf);
    entry.payload = buf;

    journal.append(entry);

    // Modify original buffer — journal should have independent copy
    buf[0] = 99;

    const auto& stored = journal.payload_bytes(0);
    EXPECT_EQ(stored.size(), 8u);
    EXPECT_EQ(stored[0], 1);  // Original value, not 99
}

TEST(JournalStubTest, BufferFullReturnsError) {
    JournalStub journal(2);  // Capacity of 2

    JournalEntry e{};
    e.sequence = 1; e.partition_id = 1;
    e.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
    e.payload_size = 0; e.payload = nullptr;

    EXPECT_EQ(journal.append(e), JournalResult::OK);
    e.sequence = 2;
    EXPECT_EQ(journal.append(e), JournalResult::OK);
    e.sequence = 3;
    EXPECT_EQ(journal.append(e), JournalResult::BUFFER_FULL);
    EXPECT_EQ(journal.entry_count(), 2u);
}

TEST(JournalStubTest, ReplayFromSequence) {
    JournalStub journal(100);

    for (SequenceNum i = 1; i <= 5; ++i) {
        JournalEntry e{};
        e.sequence = i;
        e.partition_id = 1;
        e.entry_type = JournalEntry::TYPE_INPUT_COMMAND;
        e.payload_size = 0;
        e.payload = nullptr;
        journal.append(e);
    }

    // Replay from sequence 3 — should visit entries 3, 4, 5
    struct Counter { uint64_t count = 0; SequenceNum first_seq = 0; };
    Counter ctx;

    auto visitor = [](const JournalEntry& entry, void* context) {
        auto* c = static_cast<Counter*>(context);
        if (c->count == 0) c->first_seq = entry.sequence;
        ++c->count;
    };

    uint64_t visited = journal.replay_from(3, visitor, &ctx);
    EXPECT_EQ(visited, 3u);
    EXPECT_EQ(ctx.count, 3u);
    EXPECT_EQ(ctx.first_seq, 3u);
}

TEST(JournalStubTest, FlushIsNoOp) {
    JournalStub journal(100);
    journal.flush();  // Should not crash
}

}  // namespace
}  // namespace engine

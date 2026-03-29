#include "engine/snapshot_file.h"
#include "engine/matching.h"
#include "engine/partition.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace engine {
namespace {

/// Helper: create a default InstrumentConfig for test purposes.
static InstrumentConfig make_config(InstrumentId id) {
    InstrumentConfig cfg{};
    cfg.instrument_id = id;
    cfg.tick_size = 1;
    cfg.lot_size = 1;
    cfg.min_quantity = 1;
    cfg.max_quantity = 1000000;
    cfg.static_ref_price = 10000;
    cfg.static_band_pct = 50.0;
    cfg.dynamic_band_pct = 50.0;
    cfg.residual_policy = ResidualPolicy::Cancel;
    cfg.collar_ticks = 0;
    return cfg;
}

/// Helper: insert a resting limit order into a partition and return its assigned ID.
static OrderId insert_order(Partition& part, InstrumentId inst,
                            Side side, Price price, Quantity qty,
                            ClientOrderId clord = 0, ParticipantId pid = 1) {
    PartitionMessage pm{};
    pm.type = PartitionMessage::Type::NewOrder;
    pm.instrument_id = inst;
    pm.side = side;
    pm.order_type = OrderType::Limit;
    pm.time_in_force = TimeInForce::GTC;
    pm.price = price;
    pm.quantity = qty;
    pm.client_order_id = clord;
    pm.participant_id = pid;
    pm.account_id = 0;
    pm.session_id = 0;
    pm.stp_group_id = 0;
    pm.flags = 0;
    pm.expiry_time = 0;
    pm.stop_price = 0;
    auto result = part.process(pm);
    return result.assigned_order_id;
}

class SnapshotFileTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a unique temp directory for each test
        test_dir_ = std::filesystem::temp_directory_path() / "snapshot_file_test";
        std::filesystem::create_directories(test_dir_);
        snap_path_ = (test_dir_ / "test.snap").string();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::string snap_path_;
};

// ─── Test 1: SaveAndLoad — round-trip 10 orders ────────────────

TEST_F(SnapshotFileTest, SaveAndLoad) {
    // Create partition with one instrument
    std::unordered_map<PartitionId, std::unique_ptr<Partition>> partitions;
    partitions[1] = std::make_unique<Partition>(1, 1000);
    partitions[1]->add_instrument(make_config(100));

    // Insert 10 resting orders (5 bids, 5 asks at different prices)
    std::vector<OrderId> order_ids;
    for (int i = 0; i < 5; ++i) {
        auto oid = insert_order(*partitions[1], 100, Side::Buy,
                                9000 + i * 100, 10 + i, 1000 + i, 42);
        order_ids.push_back(oid);
    }
    for (int i = 0; i < 5; ++i) {
        auto oid = insert_order(*partitions[1], 100, Side::Sell,
                                11000 + i * 100, 20 + i, 2000 + i, 43);
        order_ids.push_back(oid);
    }

    // Save snapshot
    ASSERT_TRUE(SnapshotFileWriter::save(snap_path_, partitions));

    // Load snapshot
    auto snap = SnapshotFileReader::load(snap_path_);
    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(snap->version, 1u);
    ASSERT_EQ(snap->partitions.size(), 1u);

    const auto& pd = snap->partitions[0];
    EXPECT_EQ(pd.partition_id, 1);
    EXPECT_EQ(pd.orders.size(), 10u);

    // Verify all order IDs are present
    for (const auto& od : pd.orders) {
        EXPECT_EQ(od.instrument_id, 100u);
        EXPECT_GT(od.order_id, 0u);
        EXPECT_GT(od.remaining_qty, 0u);
    }

    // Verify specific field values for first bid order
    bool found_first_bid = false;
    for (const auto& od : pd.orders) {
        if (od.client_order_id == 1000) {
            found_first_bid = true;
            EXPECT_EQ(od.side, Side::Buy);
            EXPECT_EQ(od.price, 9000);
            EXPECT_EQ(od.remaining_qty, 10u);
            EXPECT_EQ(od.participant_id, 42u);
            EXPECT_EQ(od.order_type, OrderType::Limit);
            EXPECT_EQ(od.time_in_force, TimeInForce::GTC);
        }
    }
    EXPECT_TRUE(found_first_bid) << "First bid order not found in snapshot";
}

// ─── Test 2: LoadNonExistent — returns nullopt ─────────────────

TEST_F(SnapshotFileTest, LoadNonExistent) {
    auto snap = SnapshotFileReader::load("/nonexistent/path/to/snap.bin");
    EXPECT_FALSE(snap.has_value());
}

// ─── Test 3: CorruptMagic — file with wrong magic returns nullopt

TEST_F(SnapshotFileTest, CorruptMagic) {
    // Write a file with wrong magic bytes
    std::ofstream out(snap_path_, std::ios::binary);
    const char bad_magic[] = "BADHDR01";
    out.write(bad_magic, 8);
    out.close();

    auto snap = SnapshotFileReader::load(snap_path_);
    EXPECT_FALSE(snap.has_value());
}

// ─── Test 4: EmptySnapshot — save with no orders succeeds ──────

TEST_F(SnapshotFileTest, EmptySnapshot) {
    std::unordered_map<PartitionId, std::unique_ptr<Partition>> partitions;
    partitions[1] = std::make_unique<Partition>(1, 1000);
    partitions[1]->add_instrument(make_config(100));

    // Save with zero orders
    ASSERT_TRUE(SnapshotFileWriter::save(snap_path_, partitions));

    // Load should succeed with 0 orders
    auto snap = SnapshotFileReader::load(snap_path_);
    ASSERT_TRUE(snap.has_value());
    EXPECT_EQ(snap->version, 1u);
    ASSERT_EQ(snap->partitions.size(), 1u);
    EXPECT_EQ(snap->partitions[0].orders.size(), 0u);
}

// ─── Test 5: MultiPartition — save from 3 partitions ───────────

TEST_F(SnapshotFileTest, MultiPartition) {
    std::unordered_map<PartitionId, std::unique_ptr<Partition>> partitions;

    // Create 3 partitions with different instruments
    partitions[1] = std::make_unique<Partition>(1, 1000);
    partitions[1]->add_instrument(make_config(100));

    partitions[2] = std::make_unique<Partition>(2, 1000);
    partitions[2]->add_instrument(make_config(200));

    partitions[3] = std::make_unique<Partition>(3, 1000);
    partitions[3]->add_instrument(make_config(300));

    // Insert orders into each partition
    insert_order(*partitions[1], 100, Side::Buy, 9000, 10, 1, 1);
    insert_order(*partitions[1], 100, Side::Buy, 8900, 20, 2, 1);

    insert_order(*partitions[2], 200, Side::Sell, 15000, 30, 3, 2);

    insert_order(*partitions[3], 300, Side::Buy, 5000, 50, 4, 3);
    insert_order(*partitions[3], 300, Side::Sell, 6000, 60, 5, 3);
    insert_order(*partitions[3], 300, Side::Sell, 6100, 70, 6, 3);

    // Save
    ASSERT_TRUE(SnapshotFileWriter::save(snap_path_, partitions));

    // Load
    auto snap = SnapshotFileReader::load(snap_path_);
    ASSERT_TRUE(snap.has_value());
    ASSERT_EQ(snap->partitions.size(), 3u);

    // Count total orders across all partitions
    uint64_t total = 0;
    for (const auto& pd : snap->partitions) {
        total += pd.orders.size();
    }
    EXPECT_EQ(total, 6u);

    // Verify each partition has the expected order count
    // (partition order may vary due to unordered_map iteration)
    std::unordered_map<PartitionId, size_t> counts;
    for (const auto& pd : snap->partitions) {
        counts[pd.partition_id] = pd.orders.size();
    }
    EXPECT_EQ(counts[1], 2u);
    EXPECT_EQ(counts[2], 1u);
    EXPECT_EQ(counts[3], 3u);
}

// ─── Test 6: RestoreFromSnapshot — load and re-insert orders ───

TEST_F(SnapshotFileTest, RestoreFromSnapshot) {
    // Phase 1: Create partition, insert orders, save snapshot
    {
        std::unordered_map<PartitionId, std::unique_ptr<Partition>> partitions;
        partitions[1] = std::make_unique<Partition>(1, 1000);
        partitions[1]->add_instrument(make_config(100));

        insert_order(*partitions[1], 100, Side::Buy, 9500, 100, 1, 10);
        insert_order(*partitions[1], 100, Side::Sell, 10500, 200, 2, 20);

        ASSERT_TRUE(SnapshotFileWriter::save(snap_path_, partitions));
    }

    // Phase 2: Create a fresh partition, load snapshot, re-insert
    {
        Partition part(1, 1000);
        part.add_instrument(make_config(100));

        auto snap = SnapshotFileReader::load(snap_path_);
        ASSERT_TRUE(snap.has_value());

        uint64_t restored = 0;
        for (const auto& pd : snap->partitions) {
            if (pd.partition_id != 1) continue;
            for (const auto& od : pd.orders) {
                auto* eng = part.get_engine_mut(od.instrument_id);
                ASSERT_NE(eng, nullptr);

                Order* order = part.pool().allocate();
                ASSERT_NE(order, nullptr);
                order->clear();
                order->instrument_id    = od.instrument_id;
                order->order_id         = od.order_id;
                order->client_order_id  = od.client_order_id;
                order->participant_id   = od.participant_id;
                order->account_id       = od.account_id;
                order->session_id       = od.session_id;
                order->side             = od.side;
                order->order_type       = od.order_type;
                order->time_in_force    = od.time_in_force;
                order->price            = od.price;
                order->original_qty     = od.original_qty;
                order->remaining_qty    = od.remaining_qty;
                order->display_qty      = od.display_qty;
                order->cumulative_filled = od.cumulative_filled;
                order->stop_price       = od.stop_price;
                order->trail_offset     = od.trail_offset;
                order->peg_type         = od.peg_type;
                order->peg_offset       = od.peg_offset;
                order->peg_limit        = od.peg_limit;
                order->budget           = od.budget;
                order->budget_spent     = od.budget_spent;
                order->stp_group_id     = od.stp_group_id;
                order->flags            = od.flags;
                order->entry_sequence   = od.entry_sequence;
                order->entry_timestamp  = od.entry_timestamp;
                order->expiry_time      = od.expiry_time;

                eng->book().insert_without_matching(order);
                ++restored;
            }
            part.set_sequence(pd.sequence);
        }

        EXPECT_EQ(restored, 2u);

        // Verify the book has the orders
        auto* eng = part.get_engine(100);
        ASSERT_NE(eng, nullptr);
        EXPECT_EQ(eng->book().order_count(), 2u);
        EXPECT_EQ(eng->book().best_bid(), 9500);
        EXPECT_EQ(eng->book().best_ask(), 10500);
    }
}

// ─── Test 7: TruncatedFile — returns nullopt ───────────────────

TEST_F(SnapshotFileTest, TruncatedFile) {
    // Write valid header but truncate before partition data
    std::ofstream out(snap_path_, std::ios::binary);
    const char magic[] = "MESNAP01";
    out.write(magic, 8);
    uint32_t version = 1;
    out.write(reinterpret_cast<const char*>(&version), 4);
    // Don't write the rest
    out.close();

    auto snap = SnapshotFileReader::load(snap_path_);
    EXPECT_FALSE(snap.has_value());
}

}  // namespace
}  // namespace engine

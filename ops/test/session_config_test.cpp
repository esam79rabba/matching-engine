#include "ops/session_config_store.h"
#include <gtest/gtest.h>
#include <cstdio>
#include <filesystem>

namespace ops {
namespace {

static SessionConfig make_config(const std::string& sender, uint32_t pid = 1) {
    SessionConfig cfg;
    cfg.sender_comp_id = sender;
    cfg.target_comp_id = "EXCHANGE";
    cfg.fix_version = "FIX.4.2";
    cfg.heartbeat_interval = 30;
    cfg.enabled = true;
    cfg.participant_id = pid;
    cfg.description = "Test session " + sender;
    return cfg;
}

TEST(SessionConfigStoreTest, AddAndListSessions) {
    std::string path = "/tmp/session_config_test_add.json";
    SessionConfigStore store(path);

    store.add_session(make_config("MM1", 1));
    store.add_session(make_config("MM2", 2));
    store.add_session(make_config("BROKER_A", 3));

    EXPECT_EQ(store.count(), 3u);

    auto sessions = store.list_sessions();
    EXPECT_EQ(sessions.size(), 3u);

    // Verify all are present
    bool found_mm1 = false, found_mm2 = false, found_broker = false;
    for (const auto& s : sessions) {
        if (s.sender_comp_id == "MM1") { found_mm1 = true; EXPECT_EQ(s.participant_id, 1u); }
        if (s.sender_comp_id == "MM2") { found_mm2 = true; EXPECT_EQ(s.participant_id, 2u); }
        if (s.sender_comp_id == "BROKER_A") { found_broker = true; EXPECT_EQ(s.participant_id, 3u); }
    }
    EXPECT_TRUE(found_mm1);
    EXPECT_TRUE(found_mm2);
    EXPECT_TRUE(found_broker);

    std::remove(path.c_str());
}

TEST(SessionConfigStoreTest, UpdateSession) {
    std::string path = "/tmp/session_config_test_update.json";
    SessionConfigStore store(path);

    store.add_session(make_config("MM1", 1));

    SessionConfig updated = make_config("MM1", 1);
    updated.heartbeat_interval = 60;
    updated.fix_version = "FIX.4.4";
    updated.description = "Updated session";

    EXPECT_TRUE(store.update_session("MM1", updated));

    auto cfg = store.get_session("MM1");
    ASSERT_TRUE(cfg.has_value());
    EXPECT_EQ(cfg->heartbeat_interval, 60u);
    EXPECT_EQ(cfg->fix_version, "FIX.4.4");
    EXPECT_EQ(cfg->description, "Updated session");

    // Update non-existent
    EXPECT_FALSE(store.update_session("NONEXISTENT", updated));

    std::remove(path.c_str());
}

TEST(SessionConfigStoreTest, RemoveSession) {
    std::string path = "/tmp/session_config_test_remove.json";
    SessionConfigStore store(path);

    store.add_session(make_config("MM1", 1));
    store.add_session(make_config("MM2", 2));

    EXPECT_EQ(store.count(), 2u);
    EXPECT_TRUE(store.remove_session("MM1"));
    EXPECT_EQ(store.count(), 1u);
    EXPECT_FALSE(store.get_session("MM1").has_value());
    EXPECT_TRUE(store.get_session("MM2").has_value());

    // Remove non-existent
    EXPECT_FALSE(store.remove_session("MM1"));

    std::remove(path.c_str());
}

TEST(SessionConfigStoreTest, EnableDisableSession) {
    std::string path = "/tmp/session_config_test_enable.json";
    SessionConfigStore store(path);

    store.add_session(make_config("MM1", 1));

    // Session starts enabled
    auto cfg = store.get_session("MM1");
    ASSERT_TRUE(cfg.has_value());
    EXPECT_TRUE(cfg->enabled);

    // Disable
    EXPECT_TRUE(store.disable_session("MM1"));
    cfg = store.get_session("MM1");
    ASSERT_TRUE(cfg.has_value());
    EXPECT_FALSE(cfg->enabled);

    // Enable
    EXPECT_TRUE(store.enable_session("MM1"));
    cfg = store.get_session("MM1");
    ASSERT_TRUE(cfg.has_value());
    EXPECT_TRUE(cfg->enabled);

    // Non-existent
    EXPECT_FALSE(store.enable_session("NONEXISTENT"));
    EXPECT_FALSE(store.disable_session("NONEXISTENT"));

    std::remove(path.c_str());
}

TEST(SessionConfigStoreTest, ResetSequences) {
    std::string path = "/tmp/session_config_test_reset.json";
    SessionConfigStore store(path);

    SessionConfig cfg = make_config("MM1", 1);
    cfg.inbound_seq = 100;
    cfg.outbound_seq = 200;
    store.add_session(cfg);

    auto loaded = store.get_session("MM1");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->inbound_seq, 100u);
    EXPECT_EQ(loaded->outbound_seq, 200u);

    EXPECT_TRUE(store.reset_sequences("MM1"));

    loaded = store.get_session("MM1");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->inbound_seq, 0u);
    EXPECT_EQ(loaded->outbound_seq, 0u);

    // Non-existent
    EXPECT_FALSE(store.reset_sequences("NONEXISTENT"));

    std::remove(path.c_str());
}

TEST(SessionConfigStoreTest, SaveAndLoad) {
    std::string path = "/tmp/session_config_test_persist.json";

    {
        SessionConfigStore store(path);
        store.add_session(make_config("MM1", 1));
        auto cfg2 = make_config("BROKER_A", 3);
        cfg2.heartbeat_interval = 45;
        cfg2.fix_version = "FIX.4.4";
        cfg2.enabled = false;
        store.add_session(cfg2);
        store.save();
    }

    // Load into a fresh store
    {
        SessionConfigStore store(path);
        EXPECT_EQ(store.count(), 0u);
        store.load();
        EXPECT_EQ(store.count(), 2u);

        auto mm1 = store.get_session("MM1");
        ASSERT_TRUE(mm1.has_value());
        EXPECT_EQ(mm1->target_comp_id, "EXCHANGE");
        EXPECT_EQ(mm1->fix_version, "FIX.4.2");
        EXPECT_EQ(mm1->heartbeat_interval, 30u);
        EXPECT_TRUE(mm1->enabled);
        EXPECT_EQ(mm1->participant_id, 1u);

        auto broker = store.get_session("BROKER_A");
        ASSERT_TRUE(broker.has_value());
        EXPECT_EQ(broker->heartbeat_interval, 45u);
        EXPECT_EQ(broker->fix_version, "FIX.4.4");
        EXPECT_FALSE(broker->enabled);
        EXPECT_EQ(broker->participant_id, 3u);
    }

    std::remove(path.c_str());
}

TEST(SessionConfigStoreTest, GetNonExistentReturnsEmpty) {
    std::string path = "/tmp/session_config_test_noexist.json";
    SessionConfigStore store(path);

    auto result = store.get_session("NONEXISTENT");
    EXPECT_FALSE(result.has_value());

    // Also test with some sessions present
    store.add_session(make_config("MM1", 1));
    result = store.get_session("TOTALLY_MISSING");
    EXPECT_FALSE(result.has_value());

    std::remove(path.c_str());
}

TEST(SessionConfigStoreTest, DuplicateAddOverwrites) {
    std::string path = "/tmp/session_config_test_dup.json";
    SessionConfigStore store(path);

    auto cfg1 = make_config("MM1", 1);
    cfg1.description = "Original";
    store.add_session(cfg1);

    EXPECT_EQ(store.count(), 1u);
    auto loaded = store.get_session("MM1");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->description, "Original");

    // Add again with different description
    auto cfg2 = make_config("MM1", 1);
    cfg2.description = "Overwritten";
    cfg2.heartbeat_interval = 99;
    store.add_session(cfg2);

    EXPECT_EQ(store.count(), 1u);
    loaded = store.get_session("MM1");
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->description, "Overwritten");
    EXPECT_EQ(loaded->heartbeat_interval, 99u);

    std::remove(path.c_str());
}

}  // namespace
}  // namespace ops

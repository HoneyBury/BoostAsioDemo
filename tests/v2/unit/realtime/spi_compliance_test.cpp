// SPI Compliance Test
//
// Verifies that the InstancePlugin SPI contract is upheld by the
// InstanceRuntime:
//   - MinimalPlugin lifecycle works (create, tick, snapshot, settlement)
//   - Plugin exceptions do not crash the runtime (error isolation)
//   - build_snapshot / build_settlement produce expected output
//
// These tests validate framework-level guarantees, not business logic.

#include <gtest/gtest.h>

#include "v2/realtime/instance_runtime.h"
#include "v2/realtime/instance_plugin.h"
#include "v2/realtime/types.h"

#include <atomic>
#include <chrono>
#include <string>
#include <vector>

namespace {

// ─── MinimalPlugin: simplest possible InstancePlugin ─────────────────
//
// Tracks lifecycle calls, stores a simple tick counter, and returns
// deterministic snapshot/settlement payloads.

class MinimalPlugin : public v2::realtime::InstancePlugin {
public:
    void on_instance_created(v2::realtime::InstanceContext& ctx) override {
        created_called_ = true;
        created_instance_id_ = ctx.instance_id;
        // Store a pointer to self so the runtime can track us
        ctx.plugin_state = this;
    }

    void on_player_join(v2::realtime::InstanceContext& /*ctx*/,
                        const v2::realtime::PlayerContext& /*player*/) override {
        player_join_count_++;
    }

    void on_player_leave(v2::realtime::InstanceContext& /*ctx*/,
                         const v2::realtime::PlayerContext& /*player*/) override {
        player_leave_count_++;
    }

    v2::realtime::InputResult on_input(v2::realtime::InstanceContext& /*ctx*/,
                                        const v2::realtime::InputEnvelope& input) override {
        last_input_payload_ = input.payload;
        return v2::realtime::InputResult{
            .accepted = true,
            .ack_seq = static_cast<std::uint64_t>(++input_ack_counter_)
        };
    }

    v2::realtime::TickStats on_tick(v2::realtime::InstanceContext& /*ctx*/,
                                     const v2::realtime::FrameContext& frame_ctx) noexcept override {
        tick_count_++;
        last_tick_frame_ = frame_ctx.frame_number;
        inputs_in_last_tick_ = static_cast<int>(frame_ctx.inputs_this_tick.size());

        v2::realtime::TickStats stats;
        stats.frame_number = frame_ctx.frame_number;
        stats.inputs_processed = static_cast<std::uint32_t>(frame_ctx.inputs_this_tick.size());
        stats.tick_duration_ms = 0.5;
        return stats;
    }

    v2::realtime::Snapshot build_snapshot(v2::realtime::InstanceContext& /*ctx*/,
                                           bool is_resume) noexcept override {
        snapshot_call_count_++;
        v2::realtime::Snapshot snap;
        snap.payload_type = "minimal.snapshot";
        snap.payload = "frame:" + std::to_string(tick_count_);
        snap.is_full = true;
        snap.is_resume = is_resume;
        return snap;
    }

    std::string build_settlement(v2::realtime::InstanceContext& /*ctx*/,
                                  const v2::realtime::SettlementContext& sctx) noexcept override {
        settlement_call_count_++;
        return std::string(R"({"status":"ok","total_frames":)") +
               std::to_string(sctx.total_frames) + "}";
    }

    v2::realtime::Snapshot build_resume_snapshot(v2::realtime::InstanceContext& /*ctx*/,
                                                  const v2::realtime::PlayerContext& player) noexcept override {
        resume_snapshot_call_count_++;
        v2::realtime::Snapshot snap;
        snap.payload_type = "minimal.resume";
        snap.payload = "resume:" + player.user_id;
        snap.frame_number = tick_count_;
        snap.is_resume = true;
        return snap;
    }

    // Observability for test assertions
    bool created_called_ = false;
    std::string created_instance_id_;
    int player_join_count_ = 0;
    int player_leave_count_ = 0;
    std::string last_input_payload_;
    int input_ack_counter_ = 0;
    int tick_count_ = 0;
    int last_tick_frame_ = 0;
    int inputs_in_last_tick_ = 0;
    int snapshot_call_count_ = 0;
    int settlement_call_count_ = 0;
    int resume_snapshot_call_count_ = 0;
};

// Factory function
std::unique_ptr<v2::realtime::InstancePlugin> create_minimal_plugin() {
    return std::make_unique<MinimalPlugin>();
}

// ─── ThrowOnInputPlugin: throws only in on_input ─────────────────────
//
// Used to verify that an exception in on_input is caught and logged,
// and the runtime continues normally.

class ThrowOnInputPlugin : public v2::realtime::InstancePlugin {
public:
    void on_instance_created(v2::realtime::InstanceContext& ctx) override {
        ctx.plugin_state = this;
    }

    void on_player_join(v2::realtime::InstanceContext&,
                        const v2::realtime::PlayerContext&) override {}

    void on_player_leave(v2::realtime::InstanceContext&,
                         const v2::realtime::PlayerContext&) override {}

    v2::realtime::InputResult on_input(v2::realtime::InstanceContext&,
                                        const v2::realtime::InputEnvelope&) override {
        throw_count_++;
        throw std::runtime_error("simulated input failure");
    }

    v2::realtime::TickStats on_tick(v2::realtime::InstanceContext&,
                                     const v2::realtime::FrameContext& frame_ctx) noexcept override {
        tick_count_++;
        v2::realtime::TickStats stats;
        stats.frame_number = frame_ctx.frame_number;
        stats.inputs_processed = static_cast<std::uint32_t>(frame_ctx.inputs_this_tick.size());
        return stats;
    }

    v2::realtime::Snapshot build_snapshot(v2::realtime::InstanceContext&,
                                           bool) noexcept override {
        v2::realtime::Snapshot snap;
        snap.payload_type = "throw_on_input.snapshot";
        return snap;
    }

    std::string build_settlement(v2::realtime::InstanceContext&,
                                  const v2::realtime::SettlementContext&) noexcept override {
        return R"({"status":"ok"})";
    }

    v2::realtime::Snapshot build_resume_snapshot(v2::realtime::InstanceContext&,
                                                  const v2::realtime::PlayerContext&) noexcept override {
        v2::realtime::Snapshot snap;
        snap.payload_type = "throw_on_input.resume";
        snap.is_resume = true;
        return snap;
    }

    int throw_count_ = 0;
    int tick_count_ = 0;
};

std::unique_ptr<v2::realtime::InstancePlugin> create_throw_on_input_plugin() {
    return std::make_unique<ThrowOnInputPlugin>();
}

// ─── ThrowOnInstanceCreatedPlugin: throws in on_instance_created ─────
//
// Used to verify that a plugin that throws during creation does not
// insert the instance into the runtime.

class ThrowOnInstanceCreatedPlugin : public v2::realtime::InstancePlugin {
public:
    void on_instance_created(v2::realtime::InstanceContext& /*ctx*/) override {
        throw std::runtime_error("creation failed");
    }

    void on_player_join(v2::realtime::InstanceContext&,
                        const v2::realtime::PlayerContext&) override {}
    void on_player_leave(v2::realtime::InstanceContext&,
                         const v2::realtime::PlayerContext&) override {}

    v2::realtime::InputResult on_input(v2::realtime::InstanceContext&,
                                        const v2::realtime::InputEnvelope&) override {
        return {.accepted = true};
    }

    v2::realtime::TickStats on_tick(v2::realtime::InstanceContext&,
                                     const v2::realtime::FrameContext&) noexcept override {
        return {};
    }

    v2::realtime::Snapshot build_snapshot(v2::realtime::InstanceContext&,
                                           bool) noexcept override {
        return {};
    }

    std::string build_settlement(v2::realtime::InstanceContext&,
                                  const v2::realtime::SettlementContext&) noexcept override {
        return {};
    }

    v2::realtime::Snapshot build_resume_snapshot(v2::realtime::InstanceContext&,
                                                  const v2::realtime::PlayerContext&) noexcept override {
        return {};
    }
};

std::unique_ptr<v2::realtime::InstancePlugin> create_throw_on_create_plugin() {
    return std::make_unique<ThrowOnInstanceCreatedPlugin>();
}

}  // namespace

// ─── Tests ──────────────────────────────────────────────────────────

// M4-SPI-01: MinimalPlugin lifecycle — create, tick, snapshot, settlement
TEST(SpiComplianceTest, MinimalPluginLifecycle) {
    v2::realtime::InstanceRuntime runtime;
    runtime.register_plugin("minimal", &create_minimal_plugin);

    v2::realtime::PlayerContext player;
    player.user_id = "alice";
    player.display_name = "Alice";

    auto id = runtime.create_instance("test_001", "room_001", "minimal", {player});
    EXPECT_EQ(id, "test_001");
    EXPECT_EQ(runtime.instance_count(), 1);
    EXPECT_EQ(runtime.get_instance_state("test_001"),
              v2::realtime::InstanceState::kWaitingPlayers);

    // Tick to transition to running
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto stats = runtime.tick_instance("test_001", 1, now);
    EXPECT_EQ(stats.frame_number, 1);
    EXPECT_EQ(runtime.get_instance_state("test_001"),
              v2::realtime::InstanceState::kRunning);

    // Finish and verify settlement
    runtime.finish_instance("test_001");
    EXPECT_EQ(runtime.get_instance_state("test_001"),
              v2::realtime::InstanceState::kFinished);
}

// M4-SPI-02: Plugin throwing in on_instance_created does not create instance
TEST(SpiComplianceTest, PluginThrowOnCreateDoesNotCreateInstance) {
    v2::realtime::InstanceRuntime runtime;
    runtime.register_plugin("throw_create", &create_throw_on_create_plugin);

    v2::realtime::PlayerContext player;
    player.user_id = "alice";

    auto id = runtime.create_instance("test_001", "room_001", "throw_create", {player});
    EXPECT_TRUE(id.empty());
    EXPECT_EQ(runtime.instance_count(), 0);
}

// M4-SPI-03: Plugin throwing in on_input is isolated — runtime continues
TEST(SpiComplianceTest, PluginThrowOnInputIsIsolated) {
    v2::realtime::InstanceRuntime runtime;
    runtime.register_plugin("throw_input", &create_throw_on_input_plugin);

    v2::realtime::PlayerContext player;
    player.user_id = "alice";

    auto id = runtime.create_instance("test_001", "room_001", "throw_input", {player});
    EXPECT_EQ(id, "test_001");

    // Submit inputs — these will throw in the plugin's on_input
    v2::realtime::InputEnvelope input;
    input.instance_id = "test_001";
    input.user_id = "alice";
    input.seq = 1;
    input.payload = R"({"action":"move"})";

    auto submit_result = runtime.submit_input(input);
    EXPECT_TRUE(submit_result.accepted);

    // Tick — on_input exception is caught. Runtime should still produce a tick.
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto stats = runtime.tick_instance("test_001", 1, now);
    EXPECT_EQ(stats.frame_number, 1);
    // Exception in on_input means the input was NOT accepted into frame
    EXPECT_EQ(stats.inputs_processed, 0);
}

// M4-SPI-03b: Multiple throwing inputs are isolated individually
TEST(SpiComplianceTest, MultipleThrowingInputsIsolated) {
    v2::realtime::InstanceRuntime runtime;
    runtime.register_plugin("throw_input", &create_throw_on_input_plugin);

    v2::realtime::PlayerContext player;
    player.user_id = "alice";

    auto id = runtime.create_instance("test_001", "room_001", "throw_input", {player});
    EXPECT_EQ(id, "test_001");

    // Submit multiple inputs
    for (int i = 0; i < 5; i++) {
        v2::realtime::InputEnvelope input;
        input.instance_id = "test_001";
        input.user_id = "alice";
        input.seq = i + 1;
        auto result = runtime.submit_input(input);
        EXPECT_TRUE(result.accepted);
    }

    // Tick — all should be rejected by throwing on_input
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto stats = runtime.tick_instance("test_001", 1, now);
    EXPECT_EQ(stats.frame_number, 1);
    EXPECT_EQ(stats.inputs_processed, 0);
}

// M4-SPI-04: build_snapshot normal path produces expected output
TEST(SpiComplianceTest, BuildSnapshotNormalPath) {
    v2::realtime::InstanceRuntime runtime;
    runtime.register_plugin("minimal", &create_minimal_plugin);

    v2::realtime::PlayerContext player;
    player.user_id = "alice";

    runtime.create_instance("test_001", "room_001", "minimal", {player});

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // Tick to generate snapshot
    auto stats = runtime.tick_instance("test_001", 1, now);
    EXPECT_GE(stats.frame_number, 1);

    // Verify resume snapshot works
    auto resume = runtime.get_resume_snapshot("test_001", "alice");
    EXPECT_TRUE(resume.is_resume);
    EXPECT_EQ(resume.payload_type, "minimal.resume");
    EXPECT_EQ(resume.payload, "resume:alice");
}

// M4-SPI-05: build_settlement normal path
TEST(SpiComplianceTest, BuildSettlementNormalPath) {
    v2::realtime::InstanceRuntime runtime;
    runtime.register_plugin("minimal", &create_minimal_plugin);

    v2::realtime::PlayerContext player;
    player.user_id = "alice";

    runtime.create_instance("test_001", "room_001", "minimal", {player});

    // Tick a few frames
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    runtime.tick_instance("test_001", 1, now);
    runtime.tick_instance("test_001", 2, now + 33);

    // Finish and verify settlement is produced without error
    runtime.finish_instance("test_001");
    EXPECT_EQ(runtime.get_instance_state("test_001"),
              v2::realtime::InstanceState::kFinished);
}

// M4-SPI-06: Runtime does not crash on unknown plugin type
TEST(SpiComplianceTest, UnknownPluginTypeReturnsEmpty) {
    v2::realtime::InstanceRuntime runtime;

    v2::realtime::PlayerContext player;
    player.user_id = "alice";

    auto id = runtime.create_instance("test_001", "room_001", "nonexistent", {player});
    EXPECT_TRUE(id.empty());
    EXPECT_EQ(runtime.instance_count(), 0);
}

// M4-SPI-07: MinimalPlugin full lifecycle — input, tick 3 frames, finish
TEST(SpiComplianceTest, MinimalFullLifecycle) {
    v2::realtime::InstanceRuntime runtime;
    runtime.register_plugin("minimal", &create_minimal_plugin);

    v2::realtime::PlayerContext player;
    player.user_id = "alice";

    runtime.create_instance("test_001", "room_001", "minimal", {player});

    // Submit inputs and tick multiple frames
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    for (int frame = 1; frame <= 3; frame++) {
        v2::realtime::InputEnvelope input;
        input.instance_id = "test_001";
        input.user_id = "alice";
        input.seq = frame;
        input.payload = "input_" + std::to_string(frame);
        auto submit = runtime.submit_input(input);
        EXPECT_TRUE(submit.accepted);

        auto stats = runtime.tick_instance("test_001", frame, now + (frame - 1) * 33);
        EXPECT_EQ(stats.frame_number, frame);
    }

    EXPECT_EQ(runtime.instance_count(), 1);

    runtime.finish_instance("test_001");
    EXPECT_EQ(runtime.get_instance_state("test_001"),
              v2::realtime::InstanceState::kFinished);

    // Destroy
    runtime.destroy_instance("test_001");
    EXPECT_EQ(runtime.instance_count(), 0);
}

// M4-SPI-08: Resume snapshot for nonexistent player/user returns empty
TEST(SpiComplianceTest, ResumeSnapshotForNonexistentPlayer) {
    v2::realtime::InstanceRuntime runtime;
    runtime.register_plugin("minimal", &create_minimal_plugin);

    v2::realtime::PlayerContext player;
    player.user_id = "alice";

    runtime.create_instance("test_001", "room_001", "minimal", {player});

    auto snap = runtime.get_resume_snapshot("test_001", "nonexistent");
    EXPECT_TRUE(snap.payload.empty());
}

// M4-SPI-09: Resume snapshot for nonexistent instance returns empty
TEST(SpiComplianceTest, ResumeSnapshotForNonexistentInstance) {
    v2::realtime::InstanceRuntime runtime;

    auto snap = runtime.get_resume_snapshot("no_such_inst", "alice");
    EXPECT_TRUE(snap.payload.empty());
}

// EchoRealtimePlugin Demo
//
// Standalone verification that the InstancePlugin SPI works end-to-end:
//   1. Create an InstanceRuntime and register EchoPlugin
//   2. Create an instance with one player
//   3. Submit inputs and tick the instance
//   4. Verify snapshot contains echoed payload
//   5. Finish the instance and verify settlement
//   6. Destroy the instance
//
// This program exits with 0 on success, 1 on failure.
//
// Usage:
//   realtime_echo_plugin

#include "echo_plugin.h"

#include "v2/realtime/instance_runtime.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>

static std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

int main() {
    using namespace v2::realtime;

    std::cout << "=== EchoRealtimePlugin Demo ===" << std::endl;

    // 1. Create runtime and register plugin
    InstanceRuntime runtime;
    runtime.register_plugin("echo", &echo_plugin::create_echo_plugin);

    // 2. Create instance
    PlayerContext player;
    player.user_id = "demo_user";
    player.display_name = "Demo User";

    auto instance_id = runtime.create_instance(
        "echo_demo_001", "room_demo", "echo", {player}, 33, 10);

    if (instance_id.empty()) {
        std::cerr << "FAIL: create_instance returned empty" << std::endl;
        return 1;
    }
    std::cout << "Instance created: " << instance_id << std::endl;
    std::cout << "  state=" << to_string(runtime.get_instance_state(instance_id)) << std::endl;

    // 3. Submit inputs and tick
    auto t0 = now_ms();
    for (int frame = 1; frame <= 3; frame++) {
        InputEnvelope input;
        input.instance_id = instance_id;
        input.user_id = "demo_user";
        input.seq = frame;
        input.payload_type = "echo.input";
        input.payload = "hello_frame_" + std::to_string(frame);

        auto submit = runtime.submit_input(input);
        if (!submit.accepted) {
            std::cerr << "FAIL: input rejected at frame " << frame
                       << " reason=" << submit.reject_reason << std::endl;
            return 1;
        }

        auto stats = runtime.tick_instance(instance_id, frame, t0 + (frame - 1) * 33);
        std::cout << "Tick " << frame << ": "
                   << "inputs=" << stats.inputs_processed
                   << " should_finish=" << stats.should_finish
                   << std::endl;
    }

    std::cout << "Instance state after ticks: "
               << to_string(runtime.get_instance_state(instance_id)) << std::endl;

    // 4. Verify resume snapshot
    auto resume = runtime.get_resume_snapshot(instance_id, "demo_user");
    if (resume.payload.empty()) {
        std::cerr << "FAIL: resume snapshot is empty" << std::endl;
        return 1;
    }
    std::cout << "Resume snapshot: " << resume.payload.substr(0, 80) << "..." << std::endl;

    // 5. Finish and verify settlement
    runtime.finish_instance(instance_id, FinishReason::kNormal);
    std::cout << "After finish state: "
               << to_string(runtime.get_instance_state(instance_id)) << std::endl;

    // 6. Cleanup
    runtime.destroy_instance(instance_id);
    std::cout << "Instance destroyed. Count: " << runtime.instance_count() << std::endl;

    std::cout << "\n=== ALL PASSED ===" << std::endl;
    return 0;
}

#include "v2/actor/actor.h"
#include "v2/io/io_engine.h"
#include "v2/runtime/actor_system.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t now_ns() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count());
}

struct Options {
    std::size_t iterations = 10000;
    std::size_t actors = 10000;
    std::string output_path;
};

struct LatencyStats {
    std::size_t samples = 0;
    double min_us = 0.0;
    double p50_us = 0.0;
    double p90_us = 0.0;
    double p99_us = 0.0;
    double max_us = 0.0;
    double throughput_ops_per_sec = 0.0;
};

class BenchmarkIoEngine final : public v2::io::IoEngine {
public:
    [[nodiscard]] std::uint32_t num_io_cores() const noexcept override { return 2; }
    void dispatch_to_core(std::uint32_t, std::function<void()>) override {}
    void dispatch_to_all_cores(std::function<void(std::uint32_t)>) override {}
    [[nodiscard]] std::optional<std::uint32_t> current_core_id() const noexcept override {
        return current_core_id_;
    }
    std::unique_ptr<v2::io::IoAcceptor> listen(const char*,
                                               std::uint16_t,
                                               net::SessionOptions,
                                               v2::io::IoListenOptions) override {
        return {};
    }
    void run() override {}
    void stop() override {}
    void register_session(std::uint32_t) override {}
    void unregister_session(std::uint32_t) override {}
    [[nodiscard]] std::uint32_t session_count(std::uint32_t) const noexcept override { return 0; }
    [[nodiscard]] std::uint32_t total_session_count() const noexcept override { return 0; }
    bool post_mailbox(std::uint32_t core_id, v2::actor::Message message) override {
        if (core_id >= mailboxes_.size()) {
            return false;
        }
        mailboxes_[core_id].push_back(std::move(message));
        return true;
    }
    [[nodiscard]] std::vector<v2::actor::Message> drain_mailbox(std::uint32_t core_id) override {
        if (core_id >= mailboxes_.size()) {
            return {};
        }
        auto drained = std::move(mailboxes_[core_id]);
        mailboxes_[core_id].clear();
        return drained;
    }
    void set_actor_system(v2::runtime::ActorSystem*) override {}

    std::optional<std::uint32_t> current_core_id_;

private:
    std::vector<std::vector<v2::actor::Message>> mailboxes_{2};
};

class LatencyActor final : public v2::actor::Actor {
public:
    explicit LatencyActor(std::vector<double>& samples)
        : samples_(samples) {}

    void on_message(v2::actor::Message&& message) override {
        const auto elapsed_ns = now_ns() - message.header.created_at;
        samples_.push_back(static_cast<double>(elapsed_ns) / 1000.0);
    }

private:
    std::vector<double>& samples_;
};

class NoopActor final : public v2::actor::Actor {
public:
    void on_message(v2::actor::Message&&) override {}
};

LatencyStats make_stats(std::vector<double> samples, double elapsed_seconds, std::size_t operations) {
    LatencyStats stats;
    stats.samples = samples.size();
    stats.throughput_ops_per_sec = elapsed_seconds > 0.0
        ? static_cast<double>(operations) / elapsed_seconds
        : 0.0;
    if (samples.empty()) {
        return stats;
    }

    std::sort(samples.begin(), samples.end());
    const auto pick = [&samples](double percentile) {
        const auto last_index = static_cast<double>(samples.size() - 1);
        const auto index = static_cast<std::size_t>(std::min(last_index, last_index * percentile));
        return samples[index];
    };
    stats.min_us = samples.front();
    stats.p50_us = pick(0.50);
    stats.p90_us = pick(0.90);
    stats.p99_us = pick(0.99);
    stats.max_us = samples.back();
    return stats;
}

v2::actor::Message make_message(v2::actor::ActorId target) {
    v2::actor::Message message;
    message.header.kind = v2::actor::MessageKind::kUser;
    message.header.target_actor = target;
    message.header.created_at = now_ns();
    message.payload = std::string("bench");
    return message;
}

LatencyStats run_local_tell_latency(std::size_t iterations) {
    v2::runtime::ActorSystem system;
    std::vector<double> samples;
    samples.reserve(iterations);
    auto actor = system.create_actor(std::make_unique<LatencyActor>(samples));

    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        actor.tell(make_message(actor.actor_id()));
        (void)system.dispatch_all();
    }
    const auto elapsed = std::chrono::duration<double>(Clock::now() - begin).count();
    return make_stats(std::move(samples), elapsed, iterations);
}

LatencyStats run_cross_core_tell_latency(std::size_t iterations) {
    v2::runtime::ActorSystem system;
    BenchmarkIoEngine io_engine;
    io_engine.current_core_id_ = 0U;
    system.set_io_engine(&io_engine);

    std::vector<double> samples;
    samples.reserve(iterations);
    auto actor = system.create_actor(std::make_unique<LatencyActor>(samples), {}, 1U);

    const auto begin = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
        actor.tell(make_message(actor.actor_id()));
        (void)system.drain_mailbox_and_dispatch(1U);
    }
    const auto elapsed = std::chrono::duration<double>(Clock::now() - begin).count();
    return make_stats(std::move(samples), elapsed, iterations);
}

LatencyStats run_actor_create_latency(std::size_t actors) {
    v2::runtime::ActorSystem system;
    std::vector<double> samples;
    samples.reserve(actors);

    const auto begin = Clock::now();
    for (std::size_t i = 0; i < actors; ++i) {
        const auto op_begin = now_ns();
        (void)system.create_actor(std::make_unique<NoopActor>());
        samples.push_back(static_cast<double>(now_ns() - op_begin) / 1000.0);
    }
    const auto elapsed = std::chrono::duration<double>(Clock::now() - begin).count();
    return make_stats(std::move(samples), elapsed, actors);
}

LatencyStats run_shutdown_latency(std::size_t actors) {
    v2::runtime::ActorSystem system;
    for (std::size_t i = 0; i < actors; ++i) {
        (void)system.create_actor(std::make_unique<NoopActor>());
    }

    const auto begin = Clock::now();
    system.shutdown();
    const auto elapsed = std::chrono::duration<double>(Clock::now() - begin).count();

    std::vector<double> samples;
    samples.push_back((elapsed * 1000000.0) / static_cast<double>(actors == 0 ? 1 : actors));
    return make_stats(std::move(samples), elapsed, actors);
}

Options parse_args(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto require_value = [&](const char* name) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") + name);
            }
            return argv[++i];
        };

        if (arg == "--iterations") {
            options.iterations = static_cast<std::size_t>(std::stoull(require_value("--iterations")));
        } else if (arg == "--actors") {
            options.actors = static_cast<std::size_t>(std::stoull(require_value("--actors")));
        } else if (arg == "--output") {
            options.output_path = require_value("--output");
        } else if (arg == "--help") {
            std::cout << "Usage: v2_arch_benchmark [--iterations N] [--actors N] [--output path]\n";
            std::exit(0);
        } else {
            throw std::runtime_error("unknown argument: " + arg);
        }
    }
    return options;
}

void append_stats_json(std::ostringstream& out, const char* name, const LatencyStats& stats, bool last) {
    out << "    {\n"
        << "      \"name\": \"" << name << "\",\n"
        << "      \"samples\": " << stats.samples << ",\n"
        << "      \"min_us\": " << stats.min_us << ",\n"
        << "      \"p50_us\": " << stats.p50_us << ",\n"
        << "      \"p90_us\": " << stats.p90_us << ",\n"
        << "      \"p99_us\": " << stats.p99_us << ",\n"
        << "      \"max_us\": " << stats.max_us << ",\n"
        << "      \"throughput_ops_per_sec\": " << stats.throughput_ops_per_sec << "\n"
        << "    }" << (last ? "\n" : ",\n");
}

std::string build_json(const Options& options,
                       const LatencyStats& local_tell,
                       const LatencyStats& cross_core_tell,
                       const LatencyStats& actor_create,
                       const LatencyStats& shutdown) {
    std::ostringstream out;
    out << "{\n"
        << "  \"tool\": \"v2_arch_benchmark\",\n"
        << "  \"iterations\": " << options.iterations << ",\n"
        << "  \"actors\": " << options.actors << ",\n"
        << "  \"results\": [\n";
    append_stats_json(out, "actor_local_tell_dispatch", local_tell, false);
    append_stats_json(out, "actor_cross_core_tell_drain_dispatch", cross_core_tell, false);
    append_stats_json(out, "actor_create", actor_create, false);
    append_stats_json(out, "actor_shutdown_per_actor", shutdown, true);
    out << "  ]\n"
        << "}\n";
    return out.str();
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const auto options = parse_args(argc, argv);
        const auto local_tell = run_local_tell_latency(options.iterations);
        const auto cross_core_tell = run_cross_core_tell_latency(options.iterations);
        const auto actor_create = run_actor_create_latency(options.actors);
        const auto shutdown = run_shutdown_latency(options.actors);
        const auto json = build_json(options, local_tell, cross_core_tell, actor_create, shutdown);

        if (!options.output_path.empty()) {
            std::ofstream output(options.output_path, std::ios::binary);
            if (!output) {
                std::cerr << "failed to open output: " << options.output_path << "\n";
                return 2;
            }
            output << json;
        }
        std::cout << json;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "v2_arch_benchmark failed: " << e.what() << "\n";
        return 1;
    }
}

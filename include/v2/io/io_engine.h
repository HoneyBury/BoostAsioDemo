#pragma once

#include <cstdint>
#include <functional>
#include <memory>

namespace v2::io {

class IoSession {
public:
    virtual ~IoSession() = default;
    virtual void send(const void* data, std::size_t len) = 0;
    virtual void close() = 0;
};

class IoAcceptor {
public:
    virtual ~IoAcceptor() = default;
    virtual void async_accept(
        std::function<void(std::unique_ptr<IoSession>)> on_accept) = 0;
};

class IoEngine {
public:
    virtual ~IoEngine() = default;

    // One io_context per core, each run by a dedicated thread.
    [[nodiscard]] virtual std::uint32_t num_io_cores() const noexcept = 0;

    // Strand-per-core: session dispatched to the core it was accepted on.
    virtual void dispatch_to_core(std::uint32_t core_id,
                                  std::function<void()> task) = 0;

    // Listen on address, calling on_accept on the accepting core.
    virtual std::unique_ptr<IoAcceptor> listen(
        const char* address, std::uint16_t port) = 0;

    // Run all io_contexts on their dedicated threads.
    virtual void run() = 0;
    virtual void stop() = 0;
};

}  // namespace v2::io

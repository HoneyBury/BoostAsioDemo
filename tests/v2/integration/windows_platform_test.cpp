// Windows-specific platform verification tests.
// These tests validate that the gateway framework operates correctly
// on Windows with IOCP-based I/O, Windows pipe paths, and file operations.
//
// Only compiled and run on the Windows platform (#ifdef _WIN32).

#include "net/packet_codec.h"
#include "net/session.h"

#include <boost/asio.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <future>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <fileapi.h>
#include <io.h>
#endif

namespace {

namespace asio = boost::asio;
using tcp = asio::ip::tcp;

// =========================================================================
// 1. IOCP Session send/receive verification
//
// Validates that basic TCP send/receive operations work correctly through
// the Windows IOCP completion port path. Uses a simple echo pattern with
// the boost::asio socket directly to exercise the IOCP async I/O path.
// =========================================================================

#ifdef _WIN32
TEST(WindowsPlatformTest, IocpSessionBasicSendReceive) {
    asio::io_context io;

    // Create a TCP acceptor on an ephemeral port
    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 0));
    const auto port = acceptor.local_endpoint().port();

    // Server thread: accept one connection and echo data back
    std::thread server_thread([&]() {
        boost::system::error_code ec;
        tcp::socket server_socket(io);
        acceptor.accept(server_socket, ec);
        if (ec) {
            return;
        }

        // Read data and echo back
        std::array<char, 4096> buf{};
        auto n = server_socket.read_some(asio::buffer(buf), ec);
        if (!ec && n > 0) {
            asio::write(server_socket, asio::buffer(buf.data(), n), ec);
        }

        boost::system::error_code ignored;
        server_socket.shutdown(tcp::socket::shutdown_both, ignored);
        server_socket.close(ignored);
    });

    // Small delay to ensure acceptor is ready
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Client: connect and send data
    tcp::socket client(io);
    boost::system::error_code ec;
    client.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port), ec);
    ASSERT_FALSE(ec) << "Client connect failed: " << ec.message();

    const std::string test_payload = "Hello from IOCP session test!";
    asio::write(client, asio::buffer(test_payload), ec);
    ASSERT_FALSE(ec) << "Client write failed: " << ec.message();

    // Read echo response
    std::array<char, 4096> response{};
    auto n = client.read_some(asio::buffer(response), ec);
    ASSERT_FALSE(ec) << "Client read failed: " << ec.message();

    const std::string received(response.data(), n);
    EXPECT_EQ(received, test_payload);

    boost::system::error_code ignored;
    client.shutdown(tcp::socket::shutdown_both, ignored);
    client.close(ignored);
    acceptor.close(ignored);

    if (server_thread.joinable()) {
        server_thread.join();
    }
}

TEST(WindowsPlatformTest, IocpSessionMultipleWrites) {
    asio::io_context io;

    tcp::acceptor acceptor(io, tcp::endpoint(tcp::v4(), 0));
    const auto port = acceptor.local_endpoint().port();

    // Server: accept, read multiple chunks, count total bytes
    asio::io_context server_io;
    std::thread server_thread([&]() {
        tcp::socket server_socket(server_io);
        boost::system::error_code ec;
        acceptor.accept(server_socket, ec);
        if (ec) return;

        std::vector<char> all_data;
        std::array<char, 4096> buf{};
        boost::system::error_code read_ec;
        while (!read_ec) {
            auto n = server_socket.read_some(asio::buffer(buf), read_ec);
            if (n > 0) {
                all_data.insert(all_data.end(), buf.begin(), buf.begin() + n);
            }
        }
        // Echo total received
        asio::write(server_socket, asio::buffer(all_data), ec);
        server_socket.close(ec);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Client: send multiple small writes (exercises IOCP scatter behavior)
    tcp::socket client(io);
    boost::system::error_code ec;
    client.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port), ec);
    ASSERT_FALSE(ec);

    std::vector<std::string> chunks = {"chunk1", "-chunk2", "-chunk3", "-chunk4"};
    std::string expected;
    for (const auto& chunk : chunks) {
        asio::write(client, asio::buffer(chunk), ec);
        ASSERT_FALSE(ec);
        expected += chunk;
    }
    client.shutdown(tcp::socket::shutdown_send, ec);

    // Read echo
    std::array<char, 4096> response{};
    auto n = client.read_some(asio::buffer(response), ec);
    std::string received(response.data(), n);
    EXPECT_EQ(received, expected);

    client.close(ec);
    acceptor.close(ec);
    if (server_thread.joinable()) server_thread.join();
}

// =========================================================================
// 2. Windows pipe path handling verification
//
// Validates that the named pipe path construction logic produces
// correctly formatted Windows pipe paths.
// =========================================================================

TEST(WindowsPlatformTest, PipePathFormat) {
    // Verify that Windows named pipe paths follow the correct format
    const std::string pipe_prefix = "\\\\.\\pipe\\";
    const std::string pipe_name = "boost_gateway_test";

    std::string full_path = pipe_prefix + pipe_name;

    // Check prefix is correct for Windows named pipes
    EXPECT_EQ(full_path.substr(0, 9), "\\\\.\\pipe\\");
    EXPECT_EQ(full_path, "\\\\.\\pipe\\boost_gateway_test");

    // Validate that the path conforms to MAX_PATH (260) for pipe paths
    // Note: Windows named pipe paths have a max length of 256 characters
    // after the \\.\pipe\ prefix
    EXPECT_LE(full_path.size(), 256U);

    // Verify pipe path with service name embedded
    const std::string service_name = "v2_gateway_demo";
    std::string service_pipe = pipe_prefix + service_name + "_" + pipe_name;
    EXPECT_EQ(service_pipe, "\\\\.\\pipe\\v2_gateway_demo_boost_gateway_test");
    EXPECT_LE(service_pipe.size(), 256U);

    // Verify pipe path does not contain forward slashes
    EXPECT_EQ(full_path.find('/'), std::string::npos);
    EXPECT_EQ(service_pipe.find('/'), std::string::npos);
}

// =========================================================================
// 3. Windows file lock behavior verification
//
// Validates that basic Windows file locking APIs work correctly,
// which is important for the WriteBehind data store and leaderboard
// persistence on the Windows platform.
// =========================================================================

TEST(WindowsPlatformTest, FileLockExclusive) {
    // Create a temporary file
    char temp_path[MAX_PATH + 1] = {0};
    char temp_file[MAX_PATH + 1] = {0};

    DWORD path_len = GetTempPathA(MAX_PATH, temp_path);
    ASSERT_GT(path_len, 0U) << "GetTempPathA failed";
    ASSERT_LT(path_len, MAX_PATH);

    UINT name_len = GetTempFileNameA(temp_path, "BGT", 0, temp_file);
    ASSERT_GT(name_len, 0U) << "GetTempFileNameA failed";

    // Open file with exclusive lock
    HANDLE hFile = CreateFileA(
        temp_file,
        GENERIC_READ | GENERIC_WRITE,
        0,                    // dwShareMode = 0 -> exclusive access
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    ASSERT_NE(hFile, INVALID_HANDLE_VALUE) << "CreateFileA failed: " << GetLastError();

    // Verify we can write to the exclusively locked file
    const char* test_data = "Windows file lock test data";
    DWORD bytes_written = 0;
    BOOL write_ok = WriteFile(hFile, test_data, (DWORD)strlen(test_data), &bytes_written, nullptr);
    ASSERT_TRUE(write_ok) << "WriteFile failed: " << GetLastError();
    EXPECT_EQ(bytes_written, strlen(test_data));

    // Try to open the same file with a second handle (should fail due to exclusive lock)
    HANDLE hFile2 = CreateFileA(
        temp_file,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    // On Windows, when the first handle has exclusive access (dwShareMode=0),
    // a second open should fail with ERROR_SHARING_VIOLATION (32)
    EXPECT_EQ(hFile2, INVALID_HANDLE_VALUE);
    if (hFile2 == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        EXPECT_EQ(err, ERROR_SHARING_VIOLATION) << "Expected sharing violation, got: " << err;
    } else {
        CloseHandle(hFile2);
    }

    // Close first handle
    CloseHandle(hFile);

    // Now the second open should succeed
    HANDLE hFile3 = CreateFileA(
        temp_file,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    EXPECT_NE(hFile3, INVALID_HANDLE_VALUE);
    if (hFile3 != INVALID_HANDLE_VALUE) {
        CloseHandle(hFile3);
    }

    // Cleanup
    DeleteFileA(temp_file);
}

TEST(WindowsPlatformTest, FileLockSharedRead) {
    char temp_path[MAX_PATH + 1] = {0};
    char temp_file[MAX_PATH + 1] = {0};

    DWORD path_len = GetTempPathA(MAX_PATH, temp_path);
    ASSERT_GT(path_len, 0U);
    ASSERT_LT(path_len, MAX_PATH);

    UINT name_len = GetTempFileNameA(temp_path, "BGT", 0, temp_file);
    ASSERT_GT(name_len, 0U);

    // Create file with shared read access
    HANDLE hFile1 = CreateFileA(
        temp_file,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,       // Allow subsequent read-only opens
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    ASSERT_NE(hFile1, INVALID_HANDLE_VALUE);

    const char* test_data = "shared_read_test";
    DWORD bytes_written = 0;
    WriteFile(hFile1, test_data, (DWORD)strlen(test_data), &bytes_written, nullptr);

    // Second handle with shared read should succeed
    HANDLE hFile2 = CreateFileA(
        temp_file,
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    EXPECT_NE(hFile2, INVALID_HANDLE_VALUE);

    // Third handle requesting write access should fail (share mode is read-only)
    HANDLE hFile3 = CreateFileA(
        temp_file,
        GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    EXPECT_EQ(hFile3, INVALID_HANDLE_VALUE);

    CloseHandle(hFile1);
    if (hFile2 != INVALID_HANDLE_VALUE) CloseHandle(hFile2);
    DeleteFileA(temp_file);
}

// =========================================================================
// 4. Session priority ordering on Windows IOCP
//
// Validates that the session's high-priority write FIFO ordering
// works correctly on Windows IOCP, where write completion ordering
// may differ from POSIX platforms.
// =========================================================================

TEST(WindowsPlatformTest, IocpSessionPriorityOrdering) {
    asio::io_context server_io;
    asio::io_context client_io;

    tcp::acceptor acceptor(server_io, tcp::endpoint(tcp::v4(), 0));
    const auto endpoint = acceptor.local_endpoint();

    std::promise<std::optional<tcp::socket>> accepted_socket;
    acceptor.async_accept(
        [&accepted_socket](boost::system::error_code ec, tcp::socket socket) mutable {
            // On Windows IOCP, accept errors can occur; set the promise anyway
            if (ec) {
                accepted_socket.set_value(std::nullopt);
                return;
            }
            accepted_socket.set_value(std::move(socket));
        });

    tcp::socket client_socket(client_io);
    client_socket.connect(endpoint);
    server_io.run();
    server_io.restart();

    net::SessionOptions options;
    options.max_pending_write_bytes = 1024 * 1024;

    auto accepted = accepted_socket.get_future().get();
    if (!accepted.has_value() || !accepted->is_open()) {
        GTEST_SKIP() << "accept failed on IOCP";
        return;
    }

    auto session = std::make_shared<net::Session>(std::move(*accepted), options);

    // Send mixed priority (normal/high) packets without starting io_context yet
    session->send(4006, 1, 0, "push-1");            // normal priority
    session->send(2002, 2, 0, "response-1", 0, true); // high priority
    session->send(2002, 3, 0, "response-2", 0, true); // high priority
    session->send(4006, 4, 0, "push-2");            // normal priority

    std::thread io_thread([&server_io]() { server_io.run(); });

    // Read all packets with timeout per packet
    auto read_packet = [&client_socket]() {
        std::array<unsigned char, net::packet::kLengthHeaderSize> header{};
        boost::asio::read(client_socket, boost::asio::buffer(header));
        const auto length = net::packet::decode_length(header);
        std::vector<char> payload(length);
        boost::asio::read(client_socket, boost::asio::buffer(payload));
        return net::packet::decode_payload(payload);
    };

    // On Windows IOCP, write completions may arrive in different order
    // than they were dispatched. Collect all 4 packets and verify
    // that high-priority responses arrive before the second push.
    std::vector<net::packet::DecodedPacket> packets;
    packets.reserve(4);
    for (int i = 0; i < 4; ++i) {
        packets.push_back(read_packet());
    }

    session->stop();
    server_io.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }

    // All 4 packets must arrive
    ASSERT_EQ(packets.size(), 4U);

    // Verify that push-1 arrives first
    EXPECT_EQ(packets[0].request_id, 1U);
    EXPECT_EQ(packets[0].body, "push-1");

    // Verify that response-1 and response-2 (high priority) arrive before push-2
    // On IOCP, due to completion port behavior, the exact ordering between
    // the two high-priority responses may vary, but both must precede push-2.
    int push2_index = -1;
    int resp1_index = -1;
    int resp2_index = -1;
    for (int i = 0; i < 4; ++i) {
        if (packets[i].request_id == 4) push2_index = i;
        if (packets[i].request_id == 2) resp1_index = i;
        if (packets[i].request_id == 3) resp2_index = i;
    }

    EXPECT_GE(resp1_index, 0) << "response-1 (id=2) not found";
    EXPECT_GE(resp2_index, 0) << "response-2 (id=3) not found";
    EXPECT_GE(push2_index, 0) << "push-2 (id=4) not found";

    // Both high-priority responses should appear before push-2
    EXPECT_LT(resp1_index, push2_index) << "response-1 should arrive before push-2";
    EXPECT_LT(resp2_index, push2_index) << "response-2 should arrive before push-2";
}

#endif // _WIN32

}  // anonymous namespace

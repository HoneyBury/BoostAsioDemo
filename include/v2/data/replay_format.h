#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace v2::data {

inline constexpr std::array<char, 4> kReplayMagic{'V', '2', 'R', 'P'};
inline constexpr std::array<char, 4> kResultMagic{'V', '2', 'B', 'A'};
inline constexpr std::array<char, 4> kSnapshotMagic{'V', '2', 'S', 'N'};
inline constexpr std::uint16_t kCurrentVersion = 1;

struct FormatHeader {
    std::array<char, 4> magic{};
    std::uint16_t version = 0;
    std::uint32_t payload_length = 0;
};

[[nodiscard]] std::string encode_replay(std::string_view json_payload);
[[nodiscard]] std::string encode_result(std::string_view json_payload);
[[nodiscard]] std::string encode_snapshot(std::string_view json_payload);

[[nodiscard]] std::optional<std::string> decode_replay(std::string_view binary);
[[nodiscard]] std::optional<std::string> decode_result(std::string_view binary);
[[nodiscard]] std::optional<std::string> decode_snapshot(std::string_view binary);

[[nodiscard]] std::optional<FormatHeader> decode_header(std::string_view binary);
[[nodiscard]] bool validate_header(std::string_view binary,
                                   const std::array<char, 4>& expected_magic);

}  // namespace v2::data

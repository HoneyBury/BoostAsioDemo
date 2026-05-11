#include "v2/data/replay_format.h"

#include <algorithm>
#include <cstring>

namespace v2::data {

namespace {

constexpr std::size_t kHeaderSize = 10;  // 4 magic + 2 version + 4 payload_length

std::string encode(std::string_view json_payload,
                   const std::array<char, 4>& magic) {
    std::string result;
    result.reserve(kHeaderSize + json_payload.size());
    result.append(magic.data(), magic.size());

    std::uint16_t version = kCurrentVersion;
    result.append(reinterpret_cast<const char*>(&version), sizeof(version));

    std::uint32_t payload_length = static_cast<std::uint32_t>(json_payload.size());
    result.append(reinterpret_cast<const char*>(&payload_length), sizeof(payload_length));

    result.append(json_payload);
    return result;
}

std::optional<std::string> decode(std::string_view binary,
                                   const std::array<char, 4>& expected_magic) {
    if (binary.size() < kHeaderSize) {
        return std::nullopt;
    }

    FormatHeader header;
    std::memcpy(header.magic.data(), binary.data(), 4);
    if (header.magic != expected_magic) {
        return std::nullopt;
    }

    std::memcpy(&header.version, binary.data() + 4, sizeof(std::uint16_t));
    if (header.version != kCurrentVersion) {
        return std::nullopt;
    }

    std::memcpy(&header.payload_length, binary.data() + 6, sizeof(std::uint32_t));

    if (binary.size() < kHeaderSize + header.payload_length) {
        return std::nullopt;
    }

    return std::string(binary.substr(kHeaderSize, header.payload_length));
}

}  // namespace

std::string encode_replay(std::string_view json_payload) {
    return encode(json_payload, kReplayMagic);
}

std::string encode_result(std::string_view json_payload) {
    return encode(json_payload, kResultMagic);
}

std::string encode_snapshot(std::string_view json_payload) {
    return encode(json_payload, kSnapshotMagic);
}

std::optional<std::string> decode_replay(std::string_view binary) {
    return decode(binary, kReplayMagic);
}

std::optional<std::string> decode_result(std::string_view binary) {
    return decode(binary, kResultMagic);
}

std::optional<std::string> decode_snapshot(std::string_view binary) {
    return decode(binary, kSnapshotMagic);
}

std::optional<FormatHeader> decode_header(std::string_view binary) {
    if (binary.size() < kHeaderSize) {
        return std::nullopt;
    }

    FormatHeader header;
    std::memcpy(header.magic.data(), binary.data(), 4);
    std::memcpy(&header.version, binary.data() + 4, sizeof(std::uint16_t));
    std::memcpy(&header.payload_length, binary.data() + 6, sizeof(std::uint32_t));
    return header;
}

bool validate_header(std::string_view binary,
                     const std::array<char, 4>& expected_magic) {
    if (binary.size() < kHeaderSize) {
        return false;
    }

    return std::equal(expected_magic.begin(), expected_magic.end(), binary.begin()) &&
           binary[4] == static_cast<char>(kCurrentVersion & 0xFF) &&
           binary[5] == static_cast<char>((kCurrentVersion >> 8) & 0xFF);
}

}  // namespace v2::data

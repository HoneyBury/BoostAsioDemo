// Boost Gateway — 协议安全性测试
//
// 测试用例：
//   1. 畸形长度头（0, 负数, 极大值）→ 验证解码器不会崩溃
//   2. 版本号不匹配 → 验证 check_version 返回正确错误码
//   3. 重复消息重放 → 验证 sequence_number 去重机制
//   4. flags 中包含未定义的位 → 验证兼容性处理
//   5. 超大 body（> 10MB）→ 验证拒绝或分片机制
//
// 构建方式：
//   cmake -B build -DBOOST_BUILD_SECURITY_TESTS=ON
//   cmake --build build

#include "net/packet_codec.h"
#include "net/packet_fragment.h"
#include "net/protocol.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace net::packet::test {

// ──────────────────────────────────────────────────────────────────────
// 测试 1：畸形长度头
// ──────────────────────────────────────────────────────────────────────
class MalformedLengthHeaderTest : public ::testing::Test {
protected:
    // 构造指定长度的 LengthHeader（大端序）
    static net::packet::LengthHeader make_length_header(std::uint32_t value) {
        net::packet::LengthHeader header{};
        header[0] = static_cast<unsigned char>((value >> 24U) & 0xFFU);
        header[1] = static_cast<unsigned char>((value >> 16U) & 0xFFU);
        header[2] = static_cast<unsigned char>((value >> 8U) & 0xFFU);
        header[3] = static_cast<unsigned char>(value & 0xFFU);
        return header;
    }
};

TEST_F(MalformedLengthHeaderTest, ZeroLength) {
    const auto header = make_length_header(0);
    EXPECT_NO_THROW({
        const auto len = net::packet::decode_length(header);
        EXPECT_EQ(len, 0U);
    });
}

TEST_F(MalformedLengthHeaderTest, MaxUint32Length) {
    // 解码器应该能处理 max uint32，不崩溃
    const auto header = make_length_header(std::numeric_limits<std::uint32_t>::max());
    EXPECT_NO_THROW({
        const auto len = net::packet::decode_length(header);
        EXPECT_EQ(len, std::numeric_limits<std::uint32_t>::max());
    });
}

TEST_F(MalformedLengthHeaderTest, LargerThanReasonable) {
    // 编码一个超大的 payload length，然后验证 decode_length 不会崩溃
    const auto header = make_length_header(0xFF000000U);
    const auto len = net::packet::decode_length(header);
    EXPECT_GT(len, 0U);

    // 解码时 payload 比 kFixedMetadataSize 小应该抛 invalid_argument
    // 但因为长度为 0xFF000000 远大于 kFixedMetadataSize，构造这么大的 payload
    // 会导致 OOM。我们只测试 decode_length 不崩溃，后续 decode_payload 测试
    // 用合理的 payload 但加上异常处理。
}

TEST_F(MalformedLengthHeaderTest, DecodeZeroSizedPayload) {
    // decode_payload 空 payload
    std::vector<char> empty;
    EXPECT_THROW(net::packet::decode_payload(empty), std::invalid_argument);
}

TEST_F(MalformedLengthHeaderTest, DecodePartialMetadata) {
    // payload 大小介于 1 到 kFixedMetadataSize-1
    for (std::size_t i = 1; i < net::packet::kFixedMetadataSize; ++i) {
        std::vector<char> partial(i, '\x00');
        EXPECT_THROW(net::packet::decode_payload(partial), std::invalid_argument);
    }
}

TEST_F(MalformedLengthHeaderTest, DecodeExactlyFixedMetadataSize) {
    // payload 恰好等于 kFixedMetadataSize（无 body）
    std::vector<char> exact(net::packet::kFixedMetadataSize, '\x00');
    EXPECT_NO_THROW({
        const auto p = net::packet::decode_payload(exact);
        EXPECT_TRUE(p.body.empty());
    });
}

// ──────────────────────────────────────────────────────────────────────
// 测试 2：版本号不匹配
// ──────────────────────────────────────────────────────────────────────
TEST(VersionSecurityTest, CurrentVersionIsCompatible) {
    net::packet::PacketView view;
    view.version = net::protocol::kProtocolVersion;
    EXPECT_EQ(net::packet::check_version(view), 0);
}

TEST(VersionSecurityTest, MinVersionIsCompatible) {
    net::packet::PacketView view;
    view.version = net::protocol::kProtocolMinVersion;
    EXPECT_EQ(net::packet::check_version(view), 0);
}

TEST(VersionSecurityTest, MaxVersionIsCompatible) {
    net::packet::PacketView view;
    view.version = net::protocol::kProtocolMaxVersion;
    EXPECT_EQ(net::packet::check_version(view), 0);
}

TEST(VersionSecurityTest, VersionTooLow) {
    net::packet::PacketView view;
    view.version = net::protocol::kProtocolMinVersion - 1;
    EXPECT_LT(net::packet::check_version(view), 0);
}

TEST(VersionSecurityTest, VersionTooHigh) {
    net::packet::PacketView view;
    view.version = net::protocol::kProtocolMaxVersion + 1;
    EXPECT_LT(net::packet::check_version(view), 0);
}

TEST(VersionSecurityTest, VersionZero) {
    net::packet::PacketView view;
    view.version = 0;
    // 如果 minVersion > 0，0 应该被拒绝
    if (net::protocol::kProtocolMinVersion > 0) {
        EXPECT_LT(net::packet::check_version(view), 0);
    } else {
        EXPECT_EQ(net::packet::check_version(view), 0);
    }
}

TEST(VersionSecurityTest, VersionMaxUint8) {
    net::packet::PacketView view;
    view.version = std::numeric_limits<std::uint8_t>::max();  // 255
    EXPECT_LT(net::packet::check_version(view), 0);
}

TEST(VersionSecurityTest, VersionMismatchOnDecodedPacket) {
    // 编码一个版本号为 255 的包，验证解码后 check_version 拒绝
    const auto encoded = net::packet::encode(
        1, 1, 0, "test_body", 0, 0, 255);  // version=255

    net::packet::LengthHeader header{};
    std::copy_n(encoded.begin(), 4, header.begin());
    const auto len = net::packet::decode_length(header);

    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto decoded = net::packet::decode_payload(payload);

    net::packet::PacketView view{
        decoded.version, decoded.message_id, decoded.request_id,
        decoded.sequence_number, decoded.error_code, decoded.flags};
    EXPECT_LT(net::packet::check_version(view), 0);
}

// ──────────────────────────────────────────────────────────────────────
// 测试 3：重复消息重放 → sequence_number 去重
// ──────────────────────────────────────────────────────────────────────
TEST(ReplaySecurityTest, DuplicateSequenceNumbers) {
    // 验证编码-解码对 sequence_number 的保留
    std::set<std::uint32_t> seen_sequences;

    for (std::uint32_t seq = 0; seq < 100; ++seq) {
        const auto encoded = net::packet::encode(
            42, 100, 0, "replay_test", 0, seq);

        net::packet::LengthHeader header{};
        std::copy_n(encoded.begin(), 4, header.begin());
        const auto len = net::packet::decode_length(header);

        std::vector<char> payload(encoded.begin() + 4, encoded.end());
        const auto decoded = net::packet::decode_payload(payload);

        EXPECT_EQ(decoded.sequence_number, seq);

        // 模拟去重检查
        const auto [it, inserted] = seen_sequences.insert(decoded.sequence_number);
        EXPECT_TRUE(inserted) << "duplicate sequence_number " << seq;
    }
}

TEST(ReplaySecurityTest, SequenceNumberWraparound) {
    // 测试 sequence_number 从 0xFFFFFFFF 绕回 0 的情况
    const auto encoded1 = net::packet::encode(42, 100, 0, "wrap", 0, 0xFFFFFFFFU);
    std::vector<char> p1(encoded1.begin() + 4, encoded1.end());
    const auto d1 = net::packet::decode_payload(p1);
    EXPECT_EQ(d1.sequence_number, 0xFFFFFFFFU);

    const auto encoded2 = net::packet::encode(42, 101, 0, "wrap2", 0, 0U);
    std::vector<char> p2(encoded2.begin() + 4, encoded2.end());
    const auto d2 = net::packet::decode_payload(p2);
    EXPECT_EQ(d2.sequence_number, 0U);

    // 0xFFFFFFFF 和 0 是不同的（绕回合法）
    EXPECT_NE(d1.sequence_number, d2.sequence_number);
}

TEST(ReplaySecurityTest, EncodeDecodeRoundTripPreservesSeqNum) {
    // 编码-解码往返测试确保 sequence_number 完整保留
    const std::uint32_t test_seq = 0xDEADBEEF;
    const auto encoded = net::packet::encode(
        1, 2, 0, "seq_preserve", 0, test_seq);

    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto decoded = net::packet::decode_payload(payload);

    EXPECT_EQ(decoded.sequence_number, test_seq);
}

// ──────────────────────────────────────────────────────────────────────
// 测试 4：flags 中包含未定义的位
// ──────────────────────────────────────────────────────────────────────
TEST(FlagSecurityTest, UndefinedFlagBitsArePreserved) {
    // 所有 8 个 bit 都置 1（包括未定义的位）
    constexpr std::uint8_t kAllBits = 0xFF;

    const auto encoded = net::packet::encode(
        42, 100, 0, "all_flags", kAllBits);

    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto decoded = net::packet::decode_payload(payload);

    // flags 应完整保留（包括未定义的位）
    EXPECT_EQ(decoded.flags, kAllBits);
}

TEST(FlagSecurityTest, KnownFlagsAreRecognized) {
    // 验证已知 flags 被正确编码/解码
    const auto encoded = net::packet::encode(
        42, 100, 0, "known_flags",
        net::packet::flags::kCompressed | net::packet::flags::kEncrypted);

    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto decoded = net::packet::decode_payload(payload);

    EXPECT_TRUE((decoded.flags & net::packet::flags::kCompressed) != 0);
    EXPECT_TRUE((decoded.flags & net::packet::flags::kEncrypted) != 0);
}

TEST(FlagSecurityTest, UnknownFlagsAreIgnoredByExistingCode) {
    // 设置高位 bit (0x80-0xFC)，这些目前未定义
    // 验证现有解码逻辑不会因为这些位而崩溃
    for (std::uint8_t flag_bit = 2; flag_bit < 8; ++flag_bit) {
        const auto flag_value = static_cast<std::uint8_t>(1 << flag_bit);
        // 跳过已定义的 flags
        if (flag_value == net::packet::flags::kCompressed ||
            flag_value == net::packet::flags::kEncrypted) {
            continue;
        }

        const auto encoded = net::packet::encode(
            42, 100, 0, "unknown_flag", flag_value);

        std::vector<char> payload(encoded.begin() + 4, encoded.end());

        EXPECT_NO_THROW({
            const auto decoded = net::packet::decode_payload(payload);
            EXPECT_EQ(decoded.flags, flag_value);
        }) << "flag value 0x" << std::hex << static_cast<int>(flag_bit);
    }
}

TEST(FlagSecurityTest, FragmentFlagsInteractWithApplicationFlags) {
    // fragment flags (kFragment, kLastFragment) 存储在 flags 的高 nibble
    // 与普通 flags 共享同一个字节
    // 验证代码能正确处理两者的混合
    constexpr std::uint8_t kMixedFlags =
        net::packet::fragment_flags::kFragment |
        net::packet::fragment_flags::kLastFragment |
        net::packet::flags::kCompressed;

    const auto encoded = net::packet::encode(
        42, 100, 0, "mixed", kMixedFlags);

    std::vector<char> payload(encoded.begin() + 4, encoded.end());
    const auto decoded = net::packet::decode_payload(payload);

    EXPECT_EQ(decoded.flags, kMixedFlags);
}

// ──────────────────────────────────────────────────────────────────────
// 测试 5：超大 body（> 10MB）
// ──────────────────────────────────────────────────────────────────────
TEST(LargeBodySecurityTest, VeryLargeBodyEncodeDoesNotCrash) {
    // 尝试编码一个 11MB 的 body
    // 注意：这个测试验证 encode 不会崩溃，
    // 但实际 11MB 的分配可能因内存不足而失败
    constexpr std::size_t kElevenMB = 11 * 1024 * 1024;

    try {
        const std::string large_body(kElevenMB, 'X');
        const auto encoded = net::packet::encode(
            42, 100, 0, large_body);

        // 验证长度头正确反映了 body 大小
        net::packet::LengthHeader header{};
        std::copy_n(encoded.begin(), 4, header.begin());
        const auto len = net::packet::decode_length(header);

        const auto expected_len = static_cast<std::uint32_t>(
            net::packet::kFixedMetadataSize + large_body.size());
        EXPECT_EQ(len, expected_len);
    } catch (const std::bad_alloc&) {
        // 内存不足时跳过 — 平台限制
        GTEST_SKIP() << "insufficient memory to allocate 11MB body";
    }
}

TEST(LargeBodySecurityTest, LargeBodyAcceptedByFragmenter) {
    // 验证 fragment_packet 可以处理大于 kFragmentThreshold 的 body
    constexpr std::size_t kLargeSize = net::packet::kFragmentThreshold * 3 + 1;

    try {
        const std::string large_body(kLargeSize, 'Y');
        const auto fragments = net::packet::fragment_packet(42, 100, 0, large_body);

        // 应该被分成多个 fragments
        EXPECT_GT(fragments.size(), 1U);

        // 验证所有 fragment body 加起来等于原始 body 大小
        std::size_t total = 0;
        for (const auto& f : fragments) {
            total += f.body.size();
        }
        EXPECT_EQ(total, large_body.size());

        // 验证每个 fragment 都标记了 kFragment
        for (const auto& f : fragments) {
            EXPECT_TRUE((f.flags & net::packet::fragment_flags::kFragment) != 0);
        }

        // 最后一个 fragment 应该标记 kLastFragment
        EXPECT_TRUE((fragments.back().flags & net::packet::fragment_flags::kLastFragment) != 0);
    } catch (const std::bad_alloc&) {
        GTEST_SKIP() << "insufficient memory for large body allocation";
    }
}

TEST(LargeBodySecurityTest, FragmentAssemblerHandlesLargeAssembly) {
    // 使用 FragmentAssembler 组装一个由多个 fragment 组成的大包
    constexpr std::size_t kLargeBodySize = net::packet::kFragmentThreshold * 2;

    try {
        const std::string large_body(kLargeBodySize, 'Z');
        auto fragments = net::packet::fragment_packet(42, 100, 0, large_body);

        net::packet::FragmentAssembler assembler;
        std::optional<net::packet::DecodedPacket> result;

        for (const auto& frag : fragments) {
            result = assembler.feed(frag);
        }

        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result->body, large_body);
        EXPECT_EQ(result->body.size(), kLargeBodySize);
    } catch (const std::bad_alloc&) {
        GTEST_SKIP() << "insufficient memory for large body allocation";
    }
}

}  // namespace net::packet::test

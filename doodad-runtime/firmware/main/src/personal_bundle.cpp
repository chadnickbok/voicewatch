#include "personal_bundle.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <unistd.h>
#include <utility>

namespace doodad::packages {
namespace {

constexpr std::uint8_t kBundleMagic[] = {'D', 'D', 'B', '1'};
constexpr std::uint8_t kHmacDomain[] = {
    'D', 'o', 'o', 'd', 'a', 'd', ' ', 'P', 'e', 'r', 's', 'o', 'n', 'a', 'l',
    ' ', 'B', 'u', 'n', 'd', 'l', 'e', ' ', 'v', '1', 0,
};
constexpr std::size_t kIoBlockBytes = 4096;

constexpr std::uint32_t rotate_right(std::uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32U - bits));
}

class Sha256 {
  public:
    Sha256() { reset(); }

    void reset() {
        state_ = {
            0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
            0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U,
        };
        buffered_ = 0;
        total_bytes_ = 0;
        finished_ = false;
    }

    void update(const std::uint8_t* bytes, std::size_t size) {
        if (finished_ || bytes == nullptr || size == 0) return;
        total_bytes_ += size;
        while (size > 0) {
            const auto copied = std::min(size, block_.size() - buffered_);
            std::memcpy(block_.data() + buffered_, bytes, copied);
            buffered_ += copied;
            bytes += copied;
            size -= copied;
            if (buffered_ == block_.size()) {
                transform(block_.data());
                buffered_ = 0;
            }
        }
    }

    Sha256Digest finish() {
        if (!finished_) {
            const std::uint64_t bit_count = total_bytes_ * 8U;
            block_[buffered_++] = 0x80;
            if (buffered_ > 56) {
                std::fill(block_.begin() + buffered_, block_.end(), 0);
                transform(block_.data());
                buffered_ = 0;
            }
            std::fill(block_.begin() + buffered_, block_.begin() + 56, 0);
            for (std::size_t index = 0; index < 8; ++index) {
                block_[63 - index] = static_cast<std::uint8_t>(
                    bit_count >> (index * 8));
            }
            transform(block_.data());
            finished_ = true;
        }
        Sha256Digest digest{};
        for (std::size_t word = 0; word < state_.size(); ++word) {
            for (std::size_t byte = 0; byte < 4; ++byte) {
                digest[word * 4 + byte] = static_cast<std::uint8_t>(
                    state_[word] >> (24 - byte * 8));
            }
        }
        return digest;
    }

  private:
    void transform(const std::uint8_t* block) {
        static constexpr std::array<std::uint32_t, 64> constants = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
            0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
            0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
            0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
            0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
            0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
            0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
            0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
            0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
            0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
            0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
        };
        std::array<std::uint32_t, 64> schedule{};
        for (std::size_t index = 0; index < 16; ++index) {
            schedule[index] =
                (static_cast<std::uint32_t>(block[index * 4]) << 24) |
                (static_cast<std::uint32_t>(block[index * 4 + 1]) << 16) |
                (static_cast<std::uint32_t>(block[index * 4 + 2]) << 8) |
                static_cast<std::uint32_t>(block[index * 4 + 3]);
        }
        for (std::size_t index = 16; index < schedule.size(); ++index) {
            const auto s0 = rotate_right(schedule[index - 15], 7) ^
                rotate_right(schedule[index - 15], 18) ^
                (schedule[index - 15] >> 3);
            const auto s1 = rotate_right(schedule[index - 2], 17) ^
                rotate_right(schedule[index - 2], 19) ^
                (schedule[index - 2] >> 10);
            schedule[index] = schedule[index - 16] + s0 +
                schedule[index - 7] + s1;
        }

        auto a = state_[0];
        auto b = state_[1];
        auto c = state_[2];
        auto d = state_[3];
        auto e = state_[4];
        auto f = state_[5];
        auto g = state_[6];
        auto h = state_[7];
        for (std::size_t index = 0; index < schedule.size(); ++index) {
            const auto sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                rotate_right(e, 25);
            const auto choice = (e & f) ^ ((~e) & g);
            const auto temporary1 = h + sum1 + choice + constants[index] +
                schedule[index];
            const auto sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                rotate_right(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temporary2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> block_{};
    std::size_t buffered_ = 0;
    std::uint64_t total_bytes_ = 0;
    bool finished_ = false;
};

class HmacSha256 {
  public:
    explicit HmacSha256(const std::vector<std::uint8_t>& key) {
        std::array<std::uint8_t, 64> normalized{};
        if (key.size() > normalized.size()) {
            const auto digest = sha256_bytes(key.data(), key.size());
            std::copy(digest.begin(), digest.end(), normalized.begin());
        } else if (!key.empty()) {
            std::copy(key.begin(), key.end(), normalized.begin());
        }
        std::array<std::uint8_t, 64> inner_pad{};
        for (std::size_t index = 0; index < normalized.size(); ++index) {
            inner_pad[index] = normalized[index] ^ 0x36U;
            outer_pad_[index] = normalized[index] ^ 0x5cU;
        }
        inner_.update(inner_pad.data(), inner_pad.size());
    }

    void update(const std::uint8_t* bytes, std::size_t size) {
        inner_.update(bytes, size);
    }

    Sha256Digest finish() {
        const auto inner_digest = inner_.finish();
        Sha256 outer;
        outer.update(outer_pad_.data(), outer_pad_.size());
        outer.update(inner_digest.data(), inner_digest.size());
        return outer.finish();
    }

  private:
    Sha256 inner_;
    std::array<std::uint8_t, 64> outer_pad_{};
};

bool constant_time_equal(
    const std::uint8_t* left,
    const std::uint8_t* right,
    std::size_t size) {
    std::uint8_t difference = 0;
    for (std::size_t index = 0; index < size; ++index) {
        difference |= left[index] ^ right[index];
    }
    return difference == 0;
}

std::uint32_t read_u32_be(const std::uint8_t* bytes) {
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
        (static_cast<std::uint32_t>(bytes[1]) << 16) |
        (static_cast<std::uint32_t>(bytes[2]) << 8) |
        static_cast<std::uint32_t>(bytes[3]);
}

bool ascii_identifier(const std::string& value) {
    if (value.empty() || value.size() > 128) return false;
    const auto first = static_cast<unsigned char>(value.front());
    if (!((first >= 'A' && first <= 'Z') ||
          (first >= 'a' && first <= 'z') ||
          (first >= '0' && first <= '9'))) return false;
    return std::all_of(value.begin() + 1, value.end(), [](char character) {
        const auto byte = static_cast<unsigned char>(character);
        return (byte >= 'A' && byte <= 'Z') ||
            (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') || byte == '.' || byte == '_' ||
            byte == ':' || byte == '-';
    });
}

bool app_id(const std::string& value) {
    if (value.empty() || value.size() > kMaximumPersonalAppIdBytes ||
        value.front() < 'a' || value.front() > 'z') return false;
    bool saw_dot = false;
    bool after_dot = false;
    for (std::size_t index = 1; index < value.size(); ++index) {
        const char character = value[index];
        if (character == '.') {
            if (after_dot || value[index - 1] == '.') return false;
            saw_dot = true;
            after_dot = true;
            continue;
        }
        if (character >= 'a' && character <= 'z') {
            after_dot = false;
            continue;
        }
        if (character >= '0' && character <= '9') {
            after_dot = false;
            continue;
        }
        if (character == '-' && saw_dot && !after_dot) continue;
        return false;
    }
    return saw_dot && !after_dot;
}

bool semantic_version(const std::string& value) {
    if (value.empty() ||
        value.size() > kMaximumPersonalAppVersionBytes) return false;
    std::size_t position = 0;
    for (int component = 0; component < 3; ++component) {
        const auto start = position;
        while (position < value.size() && value[position] >= '0' &&
               value[position] <= '9') ++position;
        if (position == start) return false;
        if (component < 2) {
            if (position >= value.size() || value[position++] != '.') return false;
        }
    }
    if (position == value.size()) return true;
    if (value[position] != '-' && value[position] != '+') return false;
    ++position;
    if (position == value.size()) return false;
    for (; position < value.size(); ++position) {
        const char character = value[position];
        if (!((character >= 'A' && character <= 'Z') ||
              (character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '.' ||
              character == '-')) return false;
    }
    return true;
}

bool valid_utf8(
    const std::string& value,
    std::size_t maximum_codepoints =
        std::numeric_limits<std::size_t>::max()) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(value.data());
    std::size_t position = 0;
    std::size_t codepoints = 0;
    while (position < value.size()) {
        if (++codepoints > maximum_codepoints) return false;
        const auto first = bytes[position++];
        if (first < 0x80) continue;
        std::uint32_t codepoint = 0;
        std::size_t continuation = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            codepoint = first & 0x1fU;
            continuation = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            codepoint = first & 0x0fU;
            continuation = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            codepoint = first & 0x07U;
            continuation = 3;
        } else {
            return false;
        }
        if (position + continuation > value.size()) return false;
        for (std::size_t index = 0; index < continuation; ++index) {
            const auto next = bytes[position++];
            if ((next & 0xc0U) != 0x80U) return false;
            codepoint = (codepoint << 6) | (next & 0x3fU);
        }
        if ((continuation == 2 && codepoint < 0x800) ||
            (continuation == 3 && codepoint < 0x10000) ||
            codepoint > 0x10ffff ||
            (codepoint >= 0xd800 && codepoint <= 0xdfff)) return false;
    }
    return true;
}

bool valid_display_name(const std::string& value) {
    return !value.empty() && value.size() <= kMaximumPersonalAppNameBytes &&
        valid_utf8(value, kMaximumPersonalAppNameCodepoints) &&
        std::all_of(value.begin(), value.end(), [](char character) {
            const auto byte = static_cast<unsigned char>(character);
            return byte >= 0x20 && byte != 0x7f;
        });
}

class CanonicalMetadataParser {
  public:
    CanonicalMetadataParser(const std::uint8_t* bytes, std::size_t size)
        : bytes_(bytes), size_(size) {}

    bool parse(PersonalBundleMetadata& metadata) {
        std::uint64_t number = 0;
        if (!literal("{\"app_id\":") || !string(metadata.app_id) ||
            !literal(",\"bundle_version\":") || !integer(number) || number != 1 ||
            !literal(",\"host_abi\":") || !integer(number) || number < 1 ||
            number > std::numeric_limits<std::uint32_t>::max()) return false;
        metadata.host_abi = static_cast<std::uint32_t>(number);
        std::string kind;
        if (!literal(",\"kind\":") || !string(kind) || kind != "personal" ||
            !literal(",\"name\":") || !string(metadata.name) ||
            !literal(",\"owner_id\":") || !string(metadata.owner_id) ||
            !literal(",\"payload_bytes\":") || !integer(number) || number < 1 ||
            number > kMaximumBundlePayloadBytes) return false;
        metadata.payload_bytes = static_cast<std::uint32_t>(number);
        if (!literal(",\"payload_sha256\":") ||
            !string(metadata.payload_sha256) ||
            !literal(",\"semantic_version\":") ||
            !string(metadata.semantic_version) ||
            !literal(",\"signer_key_id\":") ||
            !string(metadata.signer_key_id) || !literal("}")) return false;
        if (position_ != size_) return false;
        Sha256Digest ignored{};
        return app_id(metadata.app_id) &&
            valid_display_name(metadata.name) &&
            ascii_identifier(metadata.owner_id) &&
            ascii_identifier(metadata.signer_key_id) &&
            semantic_version(metadata.semantic_version) &&
            parse_sha256_hex(metadata.payload_sha256, ignored);
    }

  private:
    bool literal(const char* expected) {
        const auto length = std::strlen(expected);
        if (position_ + length > size_ ||
            std::memcmp(bytes_ + position_, expected, length) != 0) return false;
        position_ += length;
        return true;
    }

    bool integer(std::uint64_t& value) {
        if (position_ >= size_ || bytes_[position_] < '0' ||
            bytes_[position_] > '9') return false;
        if (bytes_[position_] == '0' && position_ + 1 < size_ &&
            bytes_[position_ + 1] >= '0' && bytes_[position_ + 1] <= '9') {
            return false;
        }
        value = 0;
        while (position_ < size_ && bytes_[position_] >= '0' &&
               bytes_[position_] <= '9') {
            const auto digit = static_cast<std::uint64_t>(bytes_[position_] - '0');
            if (value > (std::numeric_limits<std::uint64_t>::max() - digit) / 10) {
                return false;
            }
            value = value * 10 + digit;
            ++position_;
        }
        return true;
    }

    bool string(std::string& output) {
        output.clear();
        if (position_ >= size_ || bytes_[position_++] != '"') return false;
        while (position_ < size_) {
            const auto byte = bytes_[position_++];
            if (byte == '"') return valid_utf8(output);
            if (byte < 0x20) return false;
            if (byte != '\\') {
                output.push_back(static_cast<char>(byte));
                continue;
            }
            if (position_ >= size_) return false;
            const auto escaped = bytes_[position_++];
            switch (escaped) {
                case '"': output.push_back('"'); break;
                case '\\': output.push_back('\\'); break;
                case 'b': output.push_back('\b'); break;
                case 'f': output.push_back('\f'); break;
                case 'n': output.push_back('\n'); break;
                case 'r': output.push_back('\r'); break;
                case 't': output.push_back('\t'); break;
                // The outer packager uses ensure_ascii=false. None of the v1
                // metadata fields admit control characters, so a \\u escape is
                // always a non-canonical alias for a direct UTF-8/ASCII byte.
                case 'u': return false;
                default: return false;
            }
        }
        return false;
    }

    const std::uint8_t* bytes_;
    std::size_t size_;
    std::size_t position_ = 0;
};

template <typename Reader>
PersonalBundleError verify_stream(
    Reader&& read,
    std::uint64_t total_size,
    const PersonalTrustProfile& trust,
    const std::string& expected_bundle_sha256,
    VerifiedPersonalBundle& verified) {
    verified = {};
    if (!ascii_identifier(trust.owner_id) ||
        !ascii_identifier(trust.signer_key_id) || trust.hmac_key.size() != 32 ||
        trust.host_abi == 0) return PersonalBundleError::invalid_metadata;
    if (total_size < kPersonalBundleHeaderBytes + kSha256Bytes + 1) {
        return PersonalBundleError::truncated;
    }
    std::array<std::uint8_t, kPersonalBundleHeaderBytes> header{};
    if (!read(header.data(), header.size())) return PersonalBundleError::io;
    if (!constant_time_equal(header.data(), kBundleMagic, sizeof(kBundleMagic))) {
        return PersonalBundleError::unsupported_format;
    }
    const auto metadata_bytes = read_u32_be(header.data() + 4);
    const auto payload_bytes = read_u32_be(header.data() + 8);
    if (metadata_bytes > kMaximumBundleMetadataBytes ||
        payload_bytes > kMaximumBundlePayloadBytes) {
        return PersonalBundleError::oversized;
    }
    const std::uint64_t declared_size = kPersonalBundleHeaderBytes +
        static_cast<std::uint64_t>(metadata_bytes) + payload_bytes + kSha256Bytes;
    if (declared_size != total_size) return PersonalBundleError::length_mismatch;

    std::vector<std::uint8_t> metadata(metadata_bytes);
    if (!metadata.empty() && !read(metadata.data(), metadata.size())) {
        return PersonalBundleError::io;
    }
    PersonalBundleMetadata parsed;
    CanonicalMetadataParser parser(metadata.data(), metadata.size());
    if (!parser.parse(parsed) || parsed.payload_bytes != payload_bytes) {
        return PersonalBundleError::invalid_metadata;
    }
    if (parsed.owner_id != trust.owner_id) {
        return PersonalBundleError::owner_mismatch;
    }
    if (parsed.signer_key_id != trust.signer_key_id) {
        return PersonalBundleError::signer_mismatch;
    }
    if (parsed.host_abi != trust.host_abi) {
        return PersonalBundleError::host_abi_mismatch;
    }

    Sha256 bundle_hash;
    bundle_hash.update(header.data(), header.size());
    bundle_hash.update(metadata.data(), metadata.size());
    Sha256 payload_hash;
    HmacSha256 hmac(trust.hmac_key);
    hmac.update(kHmacDomain, sizeof(kHmacDomain));
    hmac.update(header.data(), header.size());
    hmac.update(metadata.data(), metadata.size());
    std::array<std::uint8_t, kIoBlockBytes> block{};
    std::uint32_t remaining = payload_bytes;
    while (remaining > 0) {
        const auto count = std::min<std::size_t>(remaining, block.size());
        if (!read(block.data(), count)) return PersonalBundleError::io;
        bundle_hash.update(block.data(), count);
        payload_hash.update(block.data(), count);
        hmac.update(block.data(), count);
        remaining -= static_cast<std::uint32_t>(count);
    }
    std::array<std::uint8_t, kSha256Bytes> supplied_tag{};
    if (!read(supplied_tag.data(), supplied_tag.size())) {
        return PersonalBundleError::io;
    }
    bundle_hash.update(supplied_tag.data(), supplied_tag.size());
    const auto calculated_tag = hmac.finish();
    if (!constant_time_equal(
            supplied_tag.data(), calculated_tag.data(), calculated_tag.size())) {
        return PersonalBundleError::invalid_hmac;
    }
    Sha256Digest expected_payload{};
    parse_sha256_hex(parsed.payload_sha256, expected_payload);
    const auto calculated_payload = payload_hash.finish();
    if (!constant_time_equal(expected_payload.data(), calculated_payload.data(),
                             calculated_payload.size())) {
        return PersonalBundleError::payload_digest_mismatch;
    }
    const auto calculated_bundle = bundle_hash.finish();
    if (!expected_bundle_sha256.empty()) {
        Sha256Digest expected_bundle{};
        if (!parse_sha256_hex(expected_bundle_sha256, expected_bundle) ||
            !constant_time_equal(expected_bundle.data(), calculated_bundle.data(),
                                 calculated_bundle.size())) {
            return PersonalBundleError::bundle_digest_mismatch;
        }
    }
    verified.metadata = std::move(parsed);
    verified.bundle_sha256 = sha256_hex(calculated_bundle);
    verified.bundle_bytes = total_size;
    verified.payload_offset = static_cast<std::uint32_t>(
        kPersonalBundleHeaderBytes + metadata_bytes);
    return PersonalBundleError::ok;
}

}  // namespace

PersonalBundleError verify_personal_bundle_file(
    const std::string& path,
    const PersonalTrustProfile& trust,
    const std::string& expected_bundle_sha256,
    VerifiedPersonalBundle& verified) {
    auto* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return PersonalBundleError::io;
    if (std::fseek(file, 0, SEEK_END) != 0) {
        std::fclose(file);
        return PersonalBundleError::io;
    }
    const long length = std::ftell(file);
    if (length < 0 || std::fseek(file, 0, SEEK_SET) != 0) {
        std::fclose(file);
        return PersonalBundleError::io;
    }
    const auto reader = [file](std::uint8_t* destination, std::size_t size) {
        return size == 0 || std::fread(destination, 1, size, file) == size;
    };
    const auto result = verify_stream(
        reader, static_cast<std::uint64_t>(length), trust,
        expected_bundle_sha256, verified);
    std::fclose(file);
    return result;
}

PersonalBundleError verify_personal_bundle_bytes(
    const std::uint8_t* bytes,
    std::size_t size,
    const PersonalTrustProfile& trust,
    const std::string& expected_bundle_sha256,
    VerifiedPersonalBundle& verified) {
    if (bytes == nullptr && size != 0) return PersonalBundleError::io;
    std::size_t position = 0;
    const auto reader = [&](std::uint8_t* destination, std::size_t count) {
        if (position > size || count > size - position) return false;
        if (count > 0) std::memcpy(destination, bytes + position, count);
        position += count;
        return true;
    };
    return verify_stream(
        reader, static_cast<std::uint64_t>(size), trust,
        expected_bundle_sha256, verified);
}

PersonalBundleError extract_personal_bundle_payload(
    const std::string& bundle_path,
    const VerifiedPersonalBundle& verified,
    const std::string& payload_part_path) {
    auto* input = std::fopen(bundle_path.c_str(), "rb");
    if (input == nullptr) return PersonalBundleError::io;
    auto* output = std::fopen(payload_part_path.c_str(), "wb");
    if (output == nullptr) {
        std::fclose(input);
        return PersonalBundleError::io;
    }
    bool ok = std::fseek(input, verified.payload_offset, SEEK_SET) == 0;
    Sha256 digest;
    std::array<std::uint8_t, kIoBlockBytes> block{};
    std::uint32_t remaining = verified.metadata.payload_bytes;
    while (ok && remaining > 0) {
        const auto count = std::min<std::size_t>(remaining, block.size());
        ok = std::fread(block.data(), 1, count, input) == count &&
            std::fwrite(block.data(), 1, count, output) == count;
        if (ok) digest.update(block.data(), count);
        remaining -= static_cast<std::uint32_t>(count);
    }
    ok = ok && std::fflush(output) == 0;
    if (ok) ok = ::fsync(::fileno(output)) == 0;
    ok = std::fclose(output) == 0 && ok;
    std::fclose(input);
    if (!ok) {
        std::remove(payload_part_path.c_str());
        return PersonalBundleError::io;
    }
    Sha256Digest expected{};
    parse_sha256_hex(verified.metadata.payload_sha256, expected);
    const auto calculated = digest.finish();
    if (!constant_time_equal(expected.data(), calculated.data(), expected.size())) {
        std::remove(payload_part_path.c_str());
        return PersonalBundleError::payload_digest_mismatch;
    }
    return PersonalBundleError::ok;
}

Sha256Digest sha256_bytes(const std::uint8_t* bytes, std::size_t size) {
    Sha256 digest;
    digest.update(bytes, size);
    return digest.finish();
}

bool sha256_file(const std::string& path, Sha256Digest& digest) {
    auto* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) return false;
    Sha256 state;
    std::array<std::uint8_t, kIoBlockBytes> block{};
    bool ok = true;
    while (true) {
        const auto count = std::fread(block.data(), 1, block.size(), file);
        state.update(block.data(), count);
        if (count < block.size()) {
            ok = std::feof(file) != 0;
            break;
        }
    }
    std::fclose(file);
    if (ok) digest = state.finish();
    return ok;
}

std::string sha256_hex(const Sha256Digest& digest) {
    static constexpr char alphabet[] = "0123456789abcdef";
    std::string value(kSha256Bytes * 2, '0');
    for (std::size_t index = 0; index < digest.size(); ++index) {
        value[index * 2] = alphabet[digest[index] >> 4];
        value[index * 2 + 1] = alphabet[digest[index] & 0x0fU];
    }
    return value;
}

bool parse_sha256_hex(const std::string& value, Sha256Digest& digest) {
    if (value.size() != kSha256Bytes * 2) return false;
    for (std::size_t index = 0; index < digest.size(); ++index) {
        const auto high = value[index * 2];
        const auto low = value[index * 2 + 1];
        const auto nibble = [](char character, std::uint8_t& output) {
            if (character >= '0' && character <= '9') {
                output = static_cast<std::uint8_t>(character - '0');
                return true;
            }
            if (character >= 'a' && character <= 'f') {
                output = static_cast<std::uint8_t>(character - 'a' + 10);
                return true;
            }
            return false;
        };
        std::uint8_t upper = 0;
        std::uint8_t lower = 0;
        if (!nibble(high, upper) || !nibble(low, lower)) return false;
        digest[index] = static_cast<std::uint8_t>((upper << 4) | lower);
    }
    return true;
}

const char* personal_bundle_error_name(PersonalBundleError error) {
    switch (error) {
        case PersonalBundleError::ok: return "ok";
        case PersonalBundleError::io: return "io";
        case PersonalBundleError::truncated: return "truncated";
        case PersonalBundleError::unsupported_format: return "unsupported_format";
        case PersonalBundleError::oversized: return "oversized";
        case PersonalBundleError::length_mismatch: return "length_mismatch";
        case PersonalBundleError::invalid_metadata: return "invalid_metadata";
        case PersonalBundleError::owner_mismatch: return "owner_mismatch";
        case PersonalBundleError::signer_mismatch: return "signer_mismatch";
        case PersonalBundleError::host_abi_mismatch: return "host_abi_mismatch";
        case PersonalBundleError::invalid_hmac: return "invalid_hmac";
        case PersonalBundleError::payload_digest_mismatch:
            return "payload_digest_mismatch";
        case PersonalBundleError::bundle_digest_mismatch:
            return "bundle_digest_mismatch";
    }
    return "unknown";
}

}  // namespace doodad::packages

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace doodad::packages {

constexpr std::size_t kSha256Bytes = 32;
constexpr std::size_t kPersonalBundleHeaderBytes = 12;
constexpr std::size_t kMaximumBundleMetadataBytes = 16 * 1024;
constexpr std::size_t kMaximumBundlePayloadBytes = 1024 * 1024;
constexpr std::size_t kMaximumPersonalAppIdBytes = 64;
constexpr std::size_t kMaximumPersonalAppVersionBytes = 64;
constexpr std::size_t kMaximumPersonalAppNameCodepoints = 48;
constexpr std::size_t kMaximumPersonalAppNameBytes =
    kMaximumPersonalAppNameCodepoints * 4;

using Sha256Digest = std::array<std::uint8_t, kSha256Bytes>;

struct PersonalTrustProfile {
    std::string owner_id;
    std::string signer_key_id;
    std::vector<std::uint8_t> hmac_key;
    std::uint32_t host_abi = 1;
};

struct PersonalBundleMetadata {
    std::string owner_id;
    std::string signer_key_id;
    std::string app_id;
    std::string name;
    std::string semantic_version;
    std::string payload_sha256;
    std::uint32_t host_abi = 0;
    std::uint32_t payload_bytes = 0;
};

struct VerifiedPersonalBundle {
    PersonalBundleMetadata metadata;
    std::string bundle_sha256;
    std::uint64_t bundle_bytes = 0;
    std::uint32_t payload_offset = 0;
};

enum class PersonalBundleError : std::uint8_t {
    ok = 0,
    io,
    truncated,
    unsupported_format,
    oversized,
    length_mismatch,
    invalid_metadata,
    owner_mismatch,
    signer_mismatch,
    host_abi_mismatch,
    invalid_hmac,
    payload_digest_mismatch,
    bundle_digest_mismatch,
};

// Verifies the exact DDB1 envelope emitted by the live-agent packager:
//
//   "DDB1" || metadata_length_be || payload_length_be
//     || canonical_json_metadata || app.wasm || hmac_sha256
//
// The HMAC input is "Doodad Personal Bundle v1\0" followed by every byte
// before the tag. File verification is streaming and does not retain app.wasm
// in memory.
PersonalBundleError verify_personal_bundle_file(
    const std::string& path,
    const PersonalTrustProfile& trust,
    const std::string& expected_bundle_sha256,
    VerifiedPersonalBundle& verified);

// Memory form used by bounded tests and tooling. It enforces exactly the same
// parser, owner binding, digest, and no-trailing-bytes rules as the file form.
PersonalBundleError verify_personal_bundle_bytes(
    const std::uint8_t* bytes,
    std::size_t size,
    const PersonalTrustProfile& trust,
    const std::string& expected_bundle_sha256,
    VerifiedPersonalBundle& verified);

// Extracts only the already-verified payload to a new staging file and checks
// its SHA-256 again. Callers still rename the staging file into place.
PersonalBundleError extract_personal_bundle_payload(
    const std::string& bundle_path,
    const VerifiedPersonalBundle& verified,
    const std::string& payload_part_path);

Sha256Digest sha256_bytes(const std::uint8_t* bytes, std::size_t size);
bool sha256_file(const std::string& path, Sha256Digest& digest);
std::string sha256_hex(const Sha256Digest& digest);
bool parse_sha256_hex(const std::string& value, Sha256Digest& digest);
const char* personal_bundle_error_name(PersonalBundleError error);

}  // namespace doodad::packages

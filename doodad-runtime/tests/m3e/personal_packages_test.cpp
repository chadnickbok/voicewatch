#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <unistd.h>

#include "package_registry.hpp"
#include "package_service.hpp"
#include "personal_bundle.hpp"

namespace {

using doodad::packages::PackageRegistry;
using doodad::packages::PackageStoreError;
using doodad::packages::PersonalBundleMetadata;
using doodad::packages::PersonalBundleError;
using doodad::packages::PersonalPackageStore;
using doodad::packages::PersonalTrustProfile;
using doodad::packages::RegistryUpdate;
using doodad::packages::VerifiedPersonalBundle;

static_assert(doodad::packages::CatalogEntry{}.app_id.size() == 65);
static_assert(doodad::packages::CatalogEntry{}.name.size() == 193);
static_assert(doodad::packages::LaunchRequest{}.app_id.size() == 65);
static_assert(doodad::packages::LaunchRequest{}.name.size() == 193);
static_assert(doodad::packages::kMaximumPersonalAppIdBytes == 64);
static_assert(doodad::packages::kMaximumPersonalAppVersionBytes == 64);
static_assert(doodad::packages::kMaximumPersonalAppNameBytes == 192);
static_assert(doodad::packages::kMaximumBundlePayloadBytes == 1024 * 1024);
static_assert(
    doodad::packages::kMaximumQuarantinedGenerationsPerApp == 8);

std::vector<std::uint8_t> from_hex(const std::string& value) {
    assert(value.size() % 2 == 0);
    std::vector<std::uint8_t> bytes(value.size() / 2);
    const auto nibble = [](char character) -> std::uint8_t {
        if (character >= '0' && character <= '9') {
            return static_cast<std::uint8_t>(character - '0');
        }
        assert(character >= 'a' && character <= 'f');
        return static_cast<std::uint8_t>(character - 'a' + 10);
    };
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] = static_cast<std::uint8_t>(
            (nibble(value[index * 2]) << 4) | nibble(value[index * 2 + 1]));
    }
    return bytes;
}

PersonalTrustProfile trust() {
    PersonalTrustProfile profile;
    profile.owner_id = "nick.local";
    profile.signer_key_id = "mac.dev.1";
    profile.host_abi = 1;
    for (std::uint8_t byte = 0; byte < 32; ++byte) {
        profile.hmac_key.push_back(byte);
    }
    return profile;
}

struct BundleFixture {
    const char* version;
    const char* payload_sha256;
    const char* bundle_sha256;
    const char* payload_hex;
    const char* tag_hex;
};

constexpr BundleFixture kFixtures[] = {
    {
        "0.1.0",
        "2a01ddca210fc5bccc7d140fcfdfe75ab7082c323b24782399a79c2137e9d571",
        "aec59e222eef99304e6cc04ceea5ea88976539f42c5c5ea896543fd456e0f0dd",
        "0061736d666978747572652d6f6e65",
        "6f46e04e656da2d0affd47a7a2d766b3785d474f93de72eb0cdfc38fd43211c2",
    },
    {
        "0.2.0",
        "0643e41e5b7b54ad8514f8a7046898038dd63be30bbe0ba17442600a397049c8",
        "85ee212a8a18de86780af877ca05cc456b024c9c60c61a133e5ca39e954f35f4",
        "0061736d666978747572652d74776f",
        "0b4173c9a5b52c40e4574268821fc1bcec0215bdd513c61c738f9576c5d4a71b",
    },
    {
        "0.3.0",
        "a68c4264a31dbc081a157aac32fbd67aadd759c60a519c6a7e63a17de5ad8279",
        "7080da12c0d51948d57018ee8482466f55177e04067b4f09bcf7ee65f18943ae",
        "0061736d666978747572652d7468726565",
        "4cd7f716e069a4add65b419fb36b8e2ba47a5f896e75dcfce7e7318cdfae1871",
    },
};

std::vector<std::uint8_t> fixture_bundle(const BundleFixture& fixture) {
    const auto payload = from_hex(fixture.payload_hex);
    const std::string metadata =
        "{\"app_id\":\"dev.doodad.rest-timer\",\"bundle_version\":1,"
        "\"host_abi\":1,\"identity\":{\"icon\":\"timer\",\"theme_seed\":\"#20BFF4\"},"
        "\"kind\":\"personal\",\"name\":\"Lift Rest\","
        "\"owner_id\":\"nick.local\",\"payload_bytes\":" +
        std::to_string(payload.size()) +
        ",\"payload_sha256\":\"" + fixture.payload_sha256 +
        "\",\"semantic_version\":\"" + fixture.version +
        "\",\"signer_key_id\":\"mac.dev.1\"}";
    assert(metadata.size() == 336);
    std::vector<std::uint8_t> bundle = {'D', 'D', 'B', '1'};
    const auto append_u32 = [&](std::uint32_t value) {
        bundle.push_back(static_cast<std::uint8_t>(value >> 24));
        bundle.push_back(static_cast<std::uint8_t>(value >> 16));
        bundle.push_back(static_cast<std::uint8_t>(value >> 8));
        bundle.push_back(static_cast<std::uint8_t>(value));
    };
    append_u32(static_cast<std::uint32_t>(metadata.size()));
    append_u32(static_cast<std::uint32_t>(payload.size()));
    bundle.insert(bundle.end(), metadata.begin(), metadata.end());
    bundle.insert(bundle.end(), payload.begin(), payload.end());
    const auto tag = from_hex(fixture.tag_hex);
    bundle.insert(bundle.end(), tag.begin(), tag.end());
    assert(doodad::packages::sha256_hex(doodad::packages::sha256_bytes(
               bundle.data(), bundle.size())) == fixture.bundle_sha256);
    return bundle;
}

doodad::packages::Sha256Digest hmac_sha256(
    const std::vector<std::uint8_t>& key,
    const std::vector<std::uint8_t>& message) {
    assert(key.size() <= 64);
    std::array<std::uint8_t, 64> inner_pad{};
    std::array<std::uint8_t, 64> outer_pad{};
    for (std::size_t index = 0; index < inner_pad.size(); ++index) {
        const auto key_byte = index < key.size() ? key[index] : 0;
        inner_pad[index] = static_cast<std::uint8_t>(key_byte ^ 0x36);
        outer_pad[index] = static_cast<std::uint8_t>(key_byte ^ 0x5c);
    }
    std::vector<std::uint8_t> inner(inner_pad.begin(), inner_pad.end());
    inner.insert(inner.end(), message.begin(), message.end());
    const auto inner_digest = doodad::packages::sha256_bytes(
        inner.data(), inner.size());
    std::vector<std::uint8_t> outer(outer_pad.begin(), outer_pad.end());
    outer.insert(outer.end(), inner_digest.begin(), inner_digest.end());
    return doodad::packages::sha256_bytes(outer.data(), outer.size());
}

std::vector<std::uint8_t> signed_metadata_bundle(
    const std::string& metadata,
    const std::vector<std::uint8_t>& payload,
    std::string& bundle_sha256) {
    std::vector<std::uint8_t> bundle = {'D', 'D', 'B', '1'};
    const auto append_u32 = [&bundle](std::uint32_t value) {
        bundle.push_back(static_cast<std::uint8_t>(value >> 24));
        bundle.push_back(static_cast<std::uint8_t>(value >> 16));
        bundle.push_back(static_cast<std::uint8_t>(value >> 8));
        bundle.push_back(static_cast<std::uint8_t>(value));
    };
    append_u32(static_cast<std::uint32_t>(metadata.size()));
    append_u32(static_cast<std::uint32_t>(payload.size()));
    bundle.insert(bundle.end(), metadata.begin(), metadata.end());
    bundle.insert(bundle.end(), payload.begin(), payload.end());

    constexpr std::uint8_t domain[] = {
        'D', 'o', 'o', 'd', 'a', 'd', ' ', 'P', 'e', 'r', 's', 'o', 'n', 'a',
        'l', ' ', 'B', 'u', 'n', 'd', 'l', 'e', ' ', 'v', '1', 0,
    };
    std::vector<std::uint8_t> authenticated(
        std::begin(domain), std::end(domain));
    authenticated.insert(
        authenticated.end(), bundle.begin(), bundle.end());
    const auto tag = hmac_sha256(trust().hmac_key, authenticated);
    bundle.insert(bundle.end(), tag.begin(), tag.end());
    bundle_sha256 = doodad::packages::sha256_hex(
        doodad::packages::sha256_bytes(bundle.data(), bundle.size()));
    return bundle;
}

std::vector<std::uint8_t> signed_fixture_bundle(
    const BundleFixture& fixture,
    const std::string& name,
    std::string& bundle_sha256) {
    const auto payload = from_hex(fixture.payload_hex);
    const std::string metadata =
        "{\"app_id\":\"dev.doodad.rest-timer\",\"bundle_version\":1,"
        "\"host_abi\":1,\"identity\":{\"icon\":\"timer\",\"theme_seed\":\"#20BFF4\"},"
        "\"kind\":\"personal\",\"name\":\"" + name +
        "\",\"owner_id\":\"nick.local\",\"payload_bytes\":" +
        std::to_string(payload.size()) +
        ",\"payload_sha256\":\"" + fixture.payload_sha256 +
        "\",\"semantic_version\":\"" + fixture.version +
        "\",\"signer_key_id\":\"mac.dev.1\"}";
    return signed_metadata_bundle(metadata, payload, bundle_sha256);
}

// Independently generated by the Python live-agent packager. This fixture
// catches cross-language header, canonical JSON, HMAC-domain, and hash drift.
std::vector<std::uint8_t> shared_cross_language_vector() {
    const std::string metadata =
        "{\"app_id\":\"dev.doodad.generated-rest\",\"bundle_version\":1,"
        "\"host_abi\":1,\"identity\":{\"icon\":\"timer\",\"theme_seed\":\"#20BFF4\"},"
        "\"kind\":\"personal\",\"name\":\"Lift Rest\","
        "\"owner_id\":\"nick.local\",\"payload_bytes\":9,"
        "\"payload_sha256\":\"90e93f21d64a418cb8437ae85094bb50ff12b7bd389ba8c47019dd670fa51743\","
        "\"semantic_version\":\"0.1.0\",\"signer_key_id\":\"macbook-v0\"}";
    assert(metadata.size() == 340);
    std::vector<std::uint8_t> bundle = from_hex("444442310000015400000009");
    bundle.insert(bundle.end(), metadata.begin(), metadata.end());
    const auto payload = from_hex("0061736d2d74657374");
    bundle.insert(bundle.end(), payload.begin(), payload.end());
    const auto tag = from_hex(
        "f7507d730c6ff6f3e0a693ddb25b116563a9b38ebe5e831cfb49be73998f5e71");
    bundle.insert(bundle.end(), tag.begin(), tag.end());
    return bundle;
}

void write_bytes(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    assert(stream.good());
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    stream.flush();
    assert(stream.good());
}

std::vector<std::uint8_t> read_bytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    assert(stream.good());
    return {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> registry_identity_record(
    const std::string& semantic_version,
    const std::string& payload_sha256) {
    assert(semantic_version.size() <= 0xffff);
    assert(payload_sha256.size() <= 0xffff);
    std::vector<std::uint8_t> record;
    const auto append_string = [&record](const std::string& value) {
        record.push_back(static_cast<std::uint8_t>(value.size() >> 8));
        record.push_back(static_cast<std::uint8_t>(value.size()));
        record.insert(record.end(), value.begin(), value.end());
    };
    append_string(semantic_version);
    append_string(payload_sha256);
    return record;
}

void repair_registry_checksum(std::vector<std::uint8_t>& encoded) {
    assert(encoded.size() >= doodad::packages::kSha256Bytes);
    const auto content_size =
        encoded.size() - doodad::packages::kSha256Bytes;
    const auto checksum = doodad::packages::sha256_bytes(
        encoded.data(), content_size);
    std::copy(
        checksum.begin(), checksum.end(), encoded.begin() + content_size);
}

class TemporaryDirectory {
  public:
    TemporaryDirectory() {
        std::array<char, 128> pattern{};
        std::snprintf(
            pattern.data(), pattern.size(), "%s/doodad-packages.XXXXXX",
            std::filesystem::temp_directory_path().c_str());
        const auto* created = ::mkdtemp(pattern.data());
        assert(created != nullptr);
        path = created;
    }
    ~TemporaryDirectory() { std::filesystem::remove_all(path); }
    std::filesystem::path path;
};

void verify_bundle_contract() {
    const auto abc = reinterpret_cast<const std::uint8_t*>("abc");
    assert(doodad::packages::sha256_hex(
               doodad::packages::sha256_bytes(abc, 3)) ==
           "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    auto profile = trust();
    profile.signer_key_id = "macbook-v0";
    const auto bundle = shared_cross_language_vector();
    assert(bundle.size() == 393);
    VerifiedPersonalBundle verified;
    assert(doodad::packages::verify_personal_bundle_bytes(
               bundle.data(), bundle.size(), profile,
               "ffb5818c5452b80be1c01c65e1413b53a481d18fb3681603626c70cfa2ec8320",
               verified) == PersonalBundleError::ok);
    assert(verified.metadata.app_id == "dev.doodad.generated-rest");
    assert(verified.metadata.icon == "timer");
    assert(verified.metadata.theme_seed == "#20BFF4");
    assert(verified.metadata.payload_bytes == 9);
    assert(verified.payload_offset == 352);

    // The same JSON value written with a printable Unicode escape is valid
    // JSON but is not the packager's canonical ensure_ascii=false encoding.
    const auto vector_payload = from_hex("0061736d2d74657374");
    const std::string escaped_metadata =
        "{\"app_id\":\"dev\\u002edoodad.generated-rest\",\"bundle_version\":1,"
        "\"host_abi\":1,\"identity\":{\"icon\":\"timer\",\"theme_seed\":\"#20BFF4\"},"
        "\"kind\":\"personal\",\"name\":\"Lift Rest\","
        "\"owner_id\":\"nick.local\",\"payload_bytes\":9,"
        "\"payload_sha256\":\"90e93f21d64a418cb8437ae85094bb50ff12b7bd389ba8c47019dd670fa51743\","
        "\"semantic_version\":\"0.1.0\",\"signer_key_id\":\"macbook-v0\"}";
    std::string escaped_bundle_sha256;
    const auto escaped_bundle = signed_metadata_bundle(
        escaped_metadata, vector_payload, escaped_bundle_sha256);
    assert(doodad::packages::verify_personal_bundle_bytes(
               escaped_bundle.data(),
               escaped_bundle.size(),
               profile,
               escaped_bundle_sha256,
               verified) == PersonalBundleError::invalid_metadata);

    auto wrong_owner = profile;
    wrong_owner.owner_id = "somebody.else";
    assert(doodad::packages::verify_personal_bundle_bytes(
               bundle.data(), bundle.size(), wrong_owner, "", verified) ==
           PersonalBundleError::owner_mismatch);
    auto wrong_signer = profile;
    wrong_signer.signer_key_id = "another-key";
    assert(doodad::packages::verify_personal_bundle_bytes(
               bundle.data(), bundle.size(), wrong_signer, "", verified) ==
           PersonalBundleError::signer_mismatch);
    auto wrong_abi = profile;
    wrong_abi.host_abi = 2;
    assert(doodad::packages::verify_personal_bundle_bytes(
               bundle.data(), bundle.size(), wrong_abi, "", verified) ==
           PersonalBundleError::host_abi_mismatch);
    auto short_key = profile;
    short_key.hmac_key.resize(31);
    assert(doodad::packages::verify_personal_bundle_bytes(
               bundle.data(), bundle.size(), short_key, "", verified) ==
           PersonalBundleError::invalid_metadata);
    auto long_key = profile;
    long_key.hmac_key.push_back(0);
    assert(doodad::packages::verify_personal_bundle_bytes(
               bundle.data(), bundle.size(), long_key, "", verified) ==
           PersonalBundleError::invalid_metadata);

    auto tampered = bundle;
    tampered[352] ^= 1;
    assert(doodad::packages::verify_personal_bundle_bytes(
               tampered.data(), tampered.size(), profile, "", verified) ==
           PersonalBundleError::invalid_hmac);
    auto invalid_app_id = bundle;
    invalid_app_id[26] = '-';
    assert(doodad::packages::verify_personal_bundle_bytes(
               invalid_app_id.data(), invalid_app_id.size(), profile, "",
               verified) == PersonalBundleError::invalid_metadata);
    tampered = bundle;
    tampered.push_back(0);
    assert(doodad::packages::verify_personal_bundle_bytes(
               tampered.data(), tampered.size(), profile, "", verified) ==
           PersonalBundleError::length_mismatch);
    assert(doodad::packages::verify_personal_bundle_bytes(
               bundle.data(), bundle.size(), profile, std::string(64, '0'),
           verified) == PersonalBundleError::bundle_digest_mismatch);
}

void verify_manifest_bounds() {
    PersonalBundleMetadata metadata;
    metadata.owner_id = "nick.local";
    metadata.signer_key_id = "mac.dev.1";
    metadata.app_id = "a." + std::string(62, 'a');
    const std::string emoji = "\xf0\x9f\x98\x80";
    for (int index = 0; index < 48; ++index) metadata.name += emoji;
    metadata.semantic_version = "1.0.0";
    metadata.icon = "generic";
    metadata.theme_seed = "#20BFF4";
    metadata.payload_sha256 = std::string(64, 'a');
    metadata.host_abi = 1;
    metadata.payload_bytes = 8;
    const auto relative = doodad::packages::package_generation_relative_path(
        metadata.app_id,
        metadata.semantic_version,
        metadata.payload_sha256);

    PackageRegistry registry("nick.local");
    assert(registry.record_install(
               metadata, std::string(64, 'b'), relative) ==
           RegistryUpdate::installed);
    const auto encoded = doodad::packages::encode_package_registry(registry);
    PackageRegistry decoded;
    assert(doodad::packages::decode_package_registry(
        encoded.data(), encoded.size(), decoded));
    assert(decoded.apps().front().app_id.size() == 64);
    assert(decoded.apps().front().current.name == metadata.name);

    auto renamed = metadata;
    renamed.name = "Renamed app";
    renamed.semantic_version = "1.1.0";
    renamed.payload_sha256 = std::string(64, 'c');
    const auto renamed_relative =
        doodad::packages::package_generation_relative_path(
            renamed.app_id,
            renamed.semantic_version,
            renamed.payload_sha256);
    assert(decoded.record_install(
               renamed, std::string(64, 'd'), renamed_relative) ==
           RegistryUpdate::installed);
    assert(decoded.apps().front().current.name == renamed.name);
    assert(decoded.apps().front().previous->name == metadata.name);
    assert(decoded.roll_back(
        metadata.app_id,
        renamed.semantic_version,
        renamed.payload_sha256));
    assert(decoded.apps().front().current.name == metadata.name);

    auto invalid = metadata;
    invalid.app_id.push_back('a');
    PackageRegistry invalid_registry("nick.local");
    assert(invalid_registry.record_install(
               invalid, std::string(64, 'b'),
               doodad::packages::package_generation_relative_path(
                   invalid.app_id,
                   invalid.semantic_version,
                   invalid.payload_sha256)) == RegistryUpdate::invalid);
    invalid = metadata;
    invalid.name += emoji;
    assert(invalid_registry.record_install(
               invalid, std::string(64, 'b'), relative) ==
           RegistryUpdate::invalid);
}

void verify_generation_identity_selection() {
    const std::string app_id = "dev.doodad.case-test";
    const std::string payload_sha256(64, 'a');
    const auto metadata = [&](const std::string& version,
                              const std::string& name) {
        PersonalBundleMetadata value;
        value.owner_id = "nick.local";
        value.signer_key_id = "mac.dev.1";
        value.app_id = app_id;
        value.name = name;
        value.semantic_version = version;
        value.icon = "generic";
        value.theme_seed = "#20BFF4";
        value.payload_sha256 = payload_sha256;
        value.host_abi = 1;
        value.payload_bytes = 8;
        return value;
    };

    const auto upper = metadata("1.0.0-RC1", "Case upper");
    const auto lower = metadata("1.0.0-rc1", "Case lower");
    const auto upper_path = doodad::packages::package_generation_relative_path(
        app_id, upper.semantic_version, payload_sha256);
    const auto lower_path = doodad::packages::package_generation_relative_path(
        app_id, lower.semantic_version, payload_sha256);
    const auto next_path = doodad::packages::package_generation_relative_path(
        app_id, "1.0.1", payload_sha256);

    // The storage key hashes a canonical sequence of three big-endian
    // length-prefixed identity fields. It is lowercase and case-invariant on
    // FAT, while case-sensitive SemVer values and same-payload releases remain
    // distinct generations.
    assert(upper_path ==
           "apps/dev.doodad.case-test/"
           "ef0c3f2c1b195327bbd6064e724da1f85c8c216b17ef02dc81a1dea7a7946fd4");
    assert(lower_path ==
           "apps/dev.doodad.case-test/"
           "e8d71e00428aa63afbc175af102d05fcc7a9048354fbe36c6facf307225be4d9");
    assert(next_path ==
           "apps/dev.doodad.case-test/"
           "d67f94153d32a2458f9be9558338bbd5d4e4cec93c8f073329c9ee40b2223d8a");
    assert(upper_path != lower_path && lower_path != next_path);
    assert(std::all_of(
        upper_path.begin(), upper_path.end(), [](char character) {
            return !(character >= 'A' && character <= 'Z');
        }));

    PackageRegistry registry("nick.local");
    assert(registry.record_install(
               upper, std::string(64, 'b'), upper_path) ==
           RegistryUpdate::installed);
    assert(registry.record_install(
               upper, std::string(64, 'b'), upper_path) ==
           RegistryUpdate::unchanged);

    // The identity triple names one immutable signed envelope. A changed name,
    // ABI, or whole-bundle digest under that same triple fails closed.
    auto changed_envelope = upper;
    changed_envelope.name = "Changed without a version";
    assert(registry.record_install(
               changed_envelope, std::string(64, 'b'), upper_path) ==
           RegistryUpdate::invalid);
    assert(registry.record_install(
               upper, std::string(64, 'c'), upper_path) ==
           RegistryUpdate::invalid);
    changed_envelope = upper;
    changed_envelope.host_abi = 2;
    assert(registry.record_install(
               changed_envelope, std::string(64, 'b'), upper_path) ==
           RegistryUpdate::invalid);

    assert(registry.record_install(
               lower, std::string(64, 'c'), lower_path) ==
           RegistryUpdate::installed);
    const auto* installed = registry.find(app_id);
    assert(installed != nullptr && installed->previous.has_value());
    assert(installed->current.semantic_version == lower.semantic_version);
    assert(installed->previous->semantic_version == upper.semantic_version);

    // Payload-only selection would choose current here. The exact tuple must
    // select and report the retained uppercase previous generation instead.
    assert(registry.mark_running(
        app_id, upper.semantic_version, payload_sha256));
    assert(registry.running()->generation.semantic_version ==
           upper.semantic_version);
    assert(!registry.mark_running(app_id, "1.0.1", payload_sha256));
    assert(!registry.roll_back(
        app_id, upper.semantic_version, payload_sha256));
    assert(registry.roll_back(
        app_id, lower.semantic_version, payload_sha256));
    assert(!registry.running().has_value());
    assert(registry.is_quarantined(
        app_id, lower.semantic_version, payload_sha256));
    assert(!registry.is_quarantined(
        app_id, upper.semantic_version, payload_sha256));
    assert(!registry.rollback_eligible(app_id));
    // This models a second Launch-now request that was queued before the first
    // request failed. Once rollback quarantines the exact tuple, neither load
    // selection nor running-state bookkeeping may select that stale request.
    assert(registry.launchable_generation(
               app_id, lower.semantic_version, payload_sha256) == nullptr);
    assert(!registry.mark_running(
        app_id, lower.semantic_version, payload_sha256));
    assert(registry.launchable_generation(
               app_id, upper.semantic_version, payload_sha256) != nullptr);

    const auto encoded = doodad::packages::encode_package_registry(registry);
    PackageRegistry restored;
    assert(doodad::packages::decode_package_registry(
        encoded.data(), encoded.size(), restored));
    assert(restored.is_quarantined(
        app_id, lower.semantic_version, payload_sha256));
    assert(!restored.is_quarantined(
        app_id, upper.semantic_version, payload_sha256));

    // A corrupt registry cannot encode current/previous as two envelopes with
    // one identity. Make the retained lowercase selector equal current and
    // repair the outer checksum; the semantic decoder still rejects it.
    auto duplicate_identity = encoded;
    const std::string from = lower.semantic_version;
    const std::string to = upper.semantic_version;
    auto found = std::search(
        duplicate_identity.begin(),
        duplicate_identity.end() - doodad::packages::kSha256Bytes,
        from.begin(),
        from.end());
    assert(found !=
           duplicate_identity.end() - doodad::packages::kSha256Bytes);
    std::copy(to.begin(), to.end(), found);
    const auto checksum = doodad::packages::sha256_bytes(
        duplicate_identity.data(),
        duplicate_identity.size() - doodad::packages::kSha256Bytes);
    std::copy(
        checksum.begin(),
        checksum.end(),
        duplicate_identity.end() - doodad::packages::kSha256Bytes);
    PackageRegistry rejected;
    assert(!doodad::packages::decode_package_registry(
        duplicate_identity.data(), duplicate_identity.size(), rejected));

    // Quarantine remains one-way after later installs rotate the failed
    // generation out of current/previous storage. A durable app.ready replay
    // cannot make that identity current again.
    const auto next = metadata("1.0.1", "Case next");
    assert(registry.record_install(
               next, std::string(64, 'd'), next_path) ==
           RegistryUpdate::installed);
    assert(registry.find(app_id)->current.semantic_version == "1.0.1");
    assert(registry.record_install(
               lower, std::string(64, 'c'), lower_path) ==
           RegistryUpdate::invalid);
    assert(registry.find(app_id)->current.semantic_version == "1.0.1");
    assert(registry.launchable_generation(
               app_id, lower.semantic_version, payload_sha256) == nullptr);
    const auto after_rotation =
        doodad::packages::encode_package_registry(registry);
    PackageRegistry after_rotation_restored;
    assert(doodad::packages::decode_package_registry(
        after_rotation.data(), after_rotation.size(), after_rotation_restored));
    assert(after_rotation_restored.is_quarantined(
        app_id, lower.semantic_version, payload_sha256));
}

void verify_noncurrent_failure_quarantine() {
    const std::string app_id = "dev.doodad.resident";
    const std::string payload_sha256(64, '5');
    const auto metadata = [&](const std::string& version) {
        PersonalBundleMetadata value;
        value.owner_id = "nick.local";
        value.signer_key_id = "mac.dev.1";
        value.app_id = app_id;
        value.name = "Resident " + version;
        value.semantic_version = version;
        value.icon = "generic";
        value.theme_seed = "#20BFF4";
        value.payload_sha256 = payload_sha256;
        value.host_abi = 1;
        value.payload_bytes = 8;
        return value;
    };
    const auto first = metadata("1.0.0");
    const auto second = metadata("1.0.1");
    const auto first_path = doodad::packages::package_generation_relative_path(
        app_id, first.semantic_version, payload_sha256);
    const auto second_path = doodad::packages::package_generation_relative_path(
        app_id, second.semantic_version, payload_sha256);
    PackageRegistry registry("nick.local");
    assert(registry.record_install(
               first, std::string(64, '6'), first_path) ==
           RegistryUpdate::installed);
    assert(registry.record_install(
               second, std::string(64, '7'), second_path) ==
           RegistryUpdate::installed);
    assert(registry.mark_running(
        app_id, first.semantic_version, payload_sha256));

    // Installation does not launch, so the resident first generation can be
    // registry previous when it fails. Quarantine it without swapping slots;
    // the distinct current generation is then the sole safe recovery target.
    const auto current_before = registry.find(app_id)->current;
    const auto previous_before = *registry.find(app_id)->previous;
    assert(registry.quarantine_generation(
        app_id, first.semantic_version, payload_sha256));
    assert(registry.find(app_id)->current == current_before);
    assert(*registry.find(app_id)->previous == previous_before);
    assert(!registry.running().has_value());
    assert(registry.launchable_generation(
               app_id, first.semantic_version, payload_sha256) == nullptr);
    const auto* safe_current = registry.launchable_generation(
        app_id, second.semantic_version, payload_sha256);
    assert(safe_current != nullptr && *safe_current == current_before);
    const auto once_quarantined =
        doodad::packages::encode_package_registry(registry);
    assert(registry.quarantine_generation(
        app_id, first.semantic_version, payload_sha256));
    assert(doodad::packages::encode_package_registry(registry) ==
           once_quarantined);
    safe_current = registry.launchable_generation(
        app_id, second.semantic_version, payload_sha256);
    assert(safe_current != nullptr && *safe_current == current_before);

    TemporaryDirectory temporary;
    PersonalPackageStore store((temporary.path / "packages").string());
    assert(store.save_registry(registry) == PackageStoreError::ok);
    PackageRegistry restored;
    assert(store.load_registry("nick.local", restored) ==
           PackageStoreError::ok);
    assert(restored.is_quarantined(
        app_id, first.semantic_version, payload_sha256));
    assert(restored.launchable_generation(
               app_id, second.semantic_version, payload_sha256) != nullptr);
}

void verify_quarantine_history() {
    const std::string app_id = "dev.doodad.failure-history";
    const auto metadata = [&](const std::string& version, char payload_digit) {
        PersonalBundleMetadata value;
        value.owner_id = "nick.local";
        value.signer_key_id = "mac.dev.1";
        value.app_id = app_id;
        value.name = "Failure history " + version;
        value.semantic_version = version;
        value.icon = "generic";
        value.theme_seed = "#20BFF4";
        value.payload_sha256 = std::string(64, payload_digit);
        value.host_abi = 1;
        value.payload_bytes = 8;
        return value;
    };
    const auto install = [](PackageRegistry& registry,
                            const PersonalBundleMetadata& generation,
                            char bundle_digit) {
        return registry.record_install(
            generation,
            std::string(64, bundle_digit),
            doodad::packages::package_generation_relative_path(
                generation.app_id,
                generation.semantic_version,
                generation.payload_sha256));
    };

    const auto first = metadata("1.0.0", '1');
    const auto second = metadata("2.0.0", '2');
    const auto third = metadata("3.0.0", '3');
    PackageRegistry registry("nick.local");
    assert(install(registry, first, 'a') == RegistryUpdate::installed);
    assert(install(registry, second, 'b') == RegistryUpdate::installed);
    assert(registry.roll_back(
        app_id, second.semantic_version, second.payload_sha256));
    assert(registry.find(app_id)->current.semantic_version ==
           first.semantic_version);

    // Installing a third generation rotates the first failed generation out
    // of the retained slots. Its selector must remain durable while a second
    // failure is added to the same app's quarantine history.
    assert(install(registry, third, 'c') == RegistryUpdate::installed);
    assert(registry.roll_back(
        app_id, third.semantic_version, third.payload_sha256));
    const auto* installed = registry.find(app_id);
    assert(installed != nullptr);
    assert(installed->current.semantic_version == first.semantic_version);
    assert(installed->previous->semantic_version == third.semantic_version);
    assert(installed->quarantined_generations.size() == 2);
    assert(registry.is_quarantined(
        app_id, second.semantic_version, second.payload_sha256));
    assert(registry.is_quarantined(
        app_id, third.semantic_version, third.payload_sha256));
    assert(install(registry, second, 'b') == RegistryUpdate::invalid);

    // Re-recording either exact failure is idempotent, including the durable
    // byte representation.
    const auto before_idempotent =
        doodad::packages::encode_package_registry(registry);
    assert(registry.quarantine_generation(
        app_id, second.semantic_version, second.payload_sha256));
    assert(doodad::packages::encode_package_registry(registry) ==
           before_idempotent);

    TemporaryDirectory temporary;
    PersonalPackageStore store((temporary.path / "packages").string());
    assert(store.save_registry(registry) == PackageStoreError::ok);
    PackageRegistry restored;
    assert(store.load_registry("nick.local", restored) ==
           PackageStoreError::ok);
    assert(restored.is_quarantined(
        app_id, second.semantic_version, second.payload_sha256));
    assert(restored.is_quarantined(
        app_id, third.semantic_version, third.payload_sha256));
    assert(install(restored, second, 'b') == RegistryUpdate::invalid);
}

void verify_quarantine_capacity_and_canonical_encoding() {
    const std::string app_id = "dev.doodad.failure-capacity";
    const auto metadata = [&](const std::string& version, char payload_digit) {
        PersonalBundleMetadata value;
        value.owner_id = "nick.local";
        value.signer_key_id = "mac.dev.1";
        value.app_id = app_id;
        value.name = "Failure capacity " + version;
        value.semantic_version = version;
        value.icon = "generic";
        value.theme_seed = "#20BFF4";
        value.payload_sha256 = std::string(64, payload_digit);
        value.host_abi = 1;
        value.payload_bytes = 8;
        return value;
    };
    const auto install = [](PackageRegistry& registry,
                            const PersonalBundleMetadata& generation,
                            char bundle_digit) {
        return registry.record_install(
            generation,
            std::string(64, bundle_digit),
            doodad::packages::package_generation_relative_path(
                generation.app_id,
                generation.semantic_version,
                generation.payload_sha256));
    };

    const auto first = metadata("1.0.0", 'e');
    const auto current = metadata("2.0.0", 'f');
    PackageRegistry registry("nick.local");
    assert(install(registry, first, 'a') == RegistryUpdate::installed);
    assert(install(registry, current, 'b') == RegistryUpdate::installed);

    // Insert in reverse order to exercise canonical sorting at mutation time.
    for (int index =
             static_cast<int>(
                 doodad::packages::kMaximumQuarantinedGenerationsPerApp) - 1;
         index >= 0;
         --index) {
        assert(registry.quarantine_generation(
            app_id,
            "0.0." + std::to_string(index),
            std::string(64, static_cast<char>('0' + index))));
    }
    const auto* installed = registry.find(app_id);
    assert(installed != nullptr);
    assert(installed->quarantined_generations.size() ==
           doodad::packages::kMaximumQuarantinedGenerationsPerApp);
    for (std::size_t index = 0;
         index < doodad::packages::kMaximumQuarantinedGenerationsPerApp;
         ++index) {
        assert(installed->quarantined_generations[index].semantic_version ==
               "0.0." + std::to_string(index));
    }

    const auto full = doodad::packages::encode_package_registry(registry);
    assert(!full.empty());
    assert(registry.quarantine_generation(
        app_id, "0.0.3", std::string(64, '3')));
    assert(doodad::packages::encode_package_registry(registry) == full);

    const auto current_before = registry.find(app_id)->current;
    const auto previous_before = *registry.find(app_id)->previous;
    assert(registry.mark_running(
        app_id, current.semantic_version, current.payload_sha256));
    assert(registry.running().has_value());

    // The ninth distinct failure is the installed current generation. There
    // is no history slot available, so consuming the failure persistently
    // blocks the whole app without evicting history or swapping either slot.
    // rollback_eligible remains a launch-target query and is therefore false,
    // while roll_back still consumes the runtime failure into saturation.
    assert(!registry.rollback_eligible(app_id));
    assert(registry.roll_back(
        app_id, current.semantic_version, current.payload_sha256));
    assert(registry.is_blocked(app_id));
    assert(!registry.running().has_value());
    assert(!registry.rollback_eligible(app_id));
    assert(registry.find(app_id)->current == current_before);
    assert(*registry.find(app_id)->previous == previous_before);
    assert(registry.find(app_id)->quarantined_generations.size() ==
           doodad::packages::kMaximumQuarantinedGenerationsPerApp);
    assert(std::none_of(
        registry.find(app_id)->quarantined_generations.begin(),
        registry.find(app_id)->quarantined_generations.end(),
        [&](const auto& identity) {
            return identity.semantic_version == current.semantic_version &&
                identity.payload_sha256 == current.payload_sha256;
        }));

    // App-level blocking rejects both retained slots, explicit running-state
    // selection, exact replay, and unrelated future installs.
    assert(registry.launchable_generation(
               app_id, current.semantic_version, current.payload_sha256) ==
           nullptr);
    assert(registry.launchable_generation(
               app_id, first.semantic_version, first.payload_sha256) ==
           nullptr);
    assert(!registry.mark_running(
        app_id, current.semantic_version, current.payload_sha256));
    assert(!registry.mark_running(
        app_id, first.semantic_version, first.payload_sha256));
    assert(install(registry, current, 'b') == RegistryUpdate::invalid);
    assert(install(registry, first, 'a') == RegistryUpdate::invalid);
    const auto future = metadata("3.0.0", 'd');
    assert(install(registry, future, 'c') == RegistryUpdate::invalid);

    const auto blocked = doodad::packages::encode_package_registry(registry);
    assert(!blocked.empty() && blocked != full);
    // Repeating the exact current failure is idempotently consumed and cannot
    // alter the terminal encoding or the retained slots.
    assert(registry.roll_back(
        app_id, current.semantic_version, current.payload_sha256));
    assert(doodad::packages::encode_package_registry(registry) == blocked);
    assert(registry.find(app_id)->current == current_before);
    assert(*registry.find(app_id)->previous == previous_before);

    PackageRegistry restored;
    assert(doodad::packages::decode_package_registry(
        blocked.data(), blocked.size(), restored));
    assert(restored.is_blocked(app_id));
    assert(!restored.rollback_eligible(app_id));
    assert(restored.find(app_id)->quarantined_generations.size() ==
           doodad::packages::kMaximumQuarantinedGenerationsPerApp);
    assert(restored.find(app_id)->current == current_before);
    assert(*restored.find(app_id)->previous == previous_before);
    assert(restored.launchable_generation(
               app_id, current.semantic_version, current.payload_sha256) ==
           nullptr);
    assert(restored.launchable_generation(
               app_id, first.semantic_version, first.payload_sha256) ==
           nullptr);
    assert(!restored.mark_running(
        app_id, current.semantic_version, current.payload_sha256));
    assert(!restored.mark_running(
        app_id, first.semantic_version, first.payload_sha256));
    assert(install(restored, current, 'b') == RegistryUpdate::invalid);
    assert(install(restored, first, 'a') == RegistryUpdate::invalid);
    assert(install(restored, future, 'c') == RegistryUpdate::invalid);

    TemporaryDirectory temporary;
    PersonalPackageStore store((temporary.path / "packages").string());
    assert(store.save_registry(registry) == PackageStoreError::ok);
    PackageRegistry durable;
    assert(store.load_registry("nick.local", durable) ==
           PackageStoreError::ok);
    assert(durable.is_blocked(app_id));
    assert(!durable.rollback_eligible(app_id));
    assert(durable.find(app_id)->quarantined_generations ==
           registry.find(app_id)->quarantined_generations);
    assert(durable.launchable_generation(
               app_id, current.semantic_version, current.payload_sha256) ==
           nullptr);
    assert(durable.launchable_generation(
               app_id, first.semantic_version, first.payload_sha256) ==
           nullptr);
    assert(install(durable, current, 'b') == RegistryUpdate::invalid);
    assert(install(durable, first, 'a') == RegistryUpdate::invalid);
    assert(install(durable, future, 'c') == RegistryUpdate::invalid);

    // DDR3 requires the set itself to be strictly sorted and unique, not just
    // semantically equivalent. Repairing the outer checksum must not make
    // either non-canonical representation decodable.
    const auto first_record = registry_identity_record(
        "0.0.0", std::string(64, '0'));
    const auto second_record = registry_identity_record(
        "0.0.1", std::string(64, '1'));
    const auto content_end = full.end() - doodad::packages::kSha256Bytes;
    const auto first_found = std::search(
        full.begin(), content_end, first_record.begin(), first_record.end());
    const auto second_found = std::search(
        full.begin(), content_end, second_record.begin(), second_record.end());
    assert(first_found != content_end && second_found != content_end);
    const auto first_offset = static_cast<std::size_t>(
        std::distance(full.begin(), first_found));
    const auto second_offset = static_cast<std::size_t>(
        std::distance(full.begin(), second_found));
    assert(first_record.size() == second_record.size());

    auto unsorted = full;
    std::swap_ranges(
        unsorted.begin() + first_offset,
        unsorted.begin() + first_offset + first_record.size(),
        unsorted.begin() + second_offset);
    repair_registry_checksum(unsorted);
    PackageRegistry rejected;
    assert(!doodad::packages::decode_package_registry(
        unsorted.data(), unsorted.size(), rejected));

    auto duplicate = full;
    std::copy(
        first_record.begin(),
        first_record.end(),
        duplicate.begin() + second_offset);
    repair_registry_checksum(duplicate);
    assert(!doodad::packages::decode_package_registry(
        duplicate.data(), duplicate.size(), rejected));

    auto over_capacity = full;
    assert(first_offset != 0 && over_capacity[first_offset - 1] ==
           doodad::packages::kMaximumQuarantinedGenerationsPerApp);
    over_capacity[first_offset - 1] = static_cast<std::uint8_t>(
        doodad::packages::kMaximumQuarantinedGenerationsPerApp + 1);
    repair_registry_checksum(over_capacity);
    assert(!doodad::packages::decode_package_registry(
        over_capacity.data(), over_capacity.size(), rejected));

    const auto last_record = registry_identity_record(
        "0.0.7", std::string(64, '7'));
    const auto blocked_content_end =
        blocked.end() - doodad::packages::kSha256Bytes;
    const auto last_found = std::search(
        blocked.begin(),
        blocked_content_end,
        last_record.begin(),
        last_record.end());
    assert(last_found != blocked_content_end);
    const auto saturated_offset = static_cast<std::size_t>(
        std::distance(blocked.begin(), last_found)) + last_record.size();
    assert(blocked[saturated_offset] == 1);
    auto invalid_saturated_flag = blocked;
    invalid_saturated_flag[saturated_offset] = 2;
    repair_registry_checksum(invalid_saturated_flag);
    assert(!doodad::packages::decode_package_registry(
        invalid_saturated_flag.data(),
        invalid_saturated_flag.size(),
        rejected));

    // Saturated is canonical only with all eight immutable history entries.
    PackageRegistry short_history("nick.local");
    assert(install(short_history, first, 'a') == RegistryUpdate::installed);
    assert(install(short_history, current, 'b') == RegistryUpdate::installed);
    for (int index = 0; index < 7; ++index) {
        assert(short_history.quarantine_generation(
            app_id,
            "0.0." + std::to_string(index),
            std::string(64, static_cast<char>('0' + index))));
    }
    short_history.find(app_id)->quarantine_saturated = true;
    assert(doodad::packages::encode_package_registry(short_history).empty());
    short_history.find(app_id)->quarantine_saturated = false;
    auto malformed_short_saturation =
        doodad::packages::encode_package_registry(short_history);
    const auto short_last_record = registry_identity_record(
        "0.0.6", std::string(64, '6'));
    const auto short_content_end = malformed_short_saturation.end() -
        doodad::packages::kSha256Bytes;
    const auto short_last_found = std::search(
        malformed_short_saturation.begin(),
        short_content_end,
        short_last_record.begin(),
        short_last_record.end());
    assert(short_last_found != short_content_end);
    const auto short_saturated_offset = static_cast<std::size_t>(
        std::distance(
            malformed_short_saturation.begin(), short_last_found)) +
        short_last_record.size();
    assert(malformed_short_saturation[short_saturated_offset] == 0);
    malformed_short_saturation[short_saturated_offset] = 1;
    repair_registry_checksum(malformed_short_saturation);
    assert(!doodad::packages::decode_package_registry(
        malformed_short_saturation.data(),
        malformed_short_saturation.size(),
        rejected));
}

PackageRegistry registry_snapshot(
    const std::string& version,
    char payload_digit,
    char bundle_digit) {
    PersonalBundleMetadata metadata;
    metadata.owner_id = "nick.local";
    metadata.signer_key_id = "mac.dev.1";
    metadata.app_id = "dev.doodad.recovery";
    metadata.name = "Recovery " + version;
    metadata.semantic_version = version;
    metadata.icon = "generic";
    metadata.theme_seed = "#20BFF4";
    metadata.payload_sha256 = std::string(64, payload_digit);
    metadata.host_abi = 1;
    metadata.payload_bytes = 8;
    PackageRegistry registry("nick.local");
    assert(registry.record_install(
               metadata,
               std::string(64, bundle_digit),
               doodad::packages::package_generation_relative_path(
                   metadata.app_id,
                   metadata.semantic_version,
                   metadata.payload_sha256)) == RegistryUpdate::installed);
    return registry;
}

void verify_registry_promotion_recovery() {
    const auto old_registry = registry_snapshot("1.0.0", '1', '2');
    const auto new_registry = registry_snapshot("2.0.0", '3', '4');
    const auto old_bytes =
        doodad::packages::encode_package_registry(old_registry);
    const auto new_bytes =
        doodad::packages::encode_package_registry(new_registry);

    const auto exercise = [&old_bytes, &new_bytes](
        const std::vector<std::uint8_t>* final_bytes,
        const std::vector<std::uint8_t>* backup_bytes,
        const std::vector<std::uint8_t>* part_bytes,
        const char* expected_version) {
        TemporaryDirectory temporary;
        const auto root = temporary.path / "packages";
        std::filesystem::create_directories(root / "incoming");
        if (final_bytes != nullptr) {
            write_bytes(root / "registry.ddr", *final_bytes);
        }
        if (backup_bytes != nullptr) {
            write_bytes(root / "registry.ddr.bak", *backup_bytes);
        }
        if (part_bytes != nullptr) {
            write_bytes(root / "registry.ddr.part", *part_bytes);
        }
        PersonalPackageStore store(root.string());
        PackageRegistry loaded;
        assert(store.load_registry("nick.local", loaded) ==
               PackageStoreError::ok);
        if (expected_version == nullptr) {
            assert(loaded.apps().empty());
            assert(!std::filesystem::exists(root / "registry.ddr"));
        } else {
            const auto* app = loaded.find("dev.doodad.recovery");
            assert(app != nullptr);
            assert(app->current.semantic_version == expected_version);
            assert(std::filesystem::is_regular_file(root / "registry.ddr"));
        }
        assert(!std::filesystem::exists(root / "registry.ddr.bak"));
        assert(!std::filesystem::exists(root / "registry.ddr.part"));
    };

    // Unique filesystem states after each FAT-compatible promotion step:
    // write part, move final to backup, promote part, and clean the backup.
    exercise(&old_bytes, nullptr, &new_bytes, "1.0.0");
    exercise(nullptr, &old_bytes, &new_bytes, "1.0.0");
    exercise(&new_bytes, &old_bytes, nullptr, "2.0.0");
    exercise(&new_bytes, nullptr, nullptr, "2.0.0");

    // A valid final is always authoritative over stale auxiliaries.
    exercise(&old_bytes, &new_bytes, &new_bytes, "1.0.0");
    // A torn/corrupt final is recoverable from the last committed backup.
    auto corrupt = new_bytes;
    corrupt.back() ^= 1;
    exercise(&corrupt, &old_bytes, &new_bytes, "1.0.0");
    // A first-install part has no commit point and must not be promoted on boot.
    exercise(nullptr, nullptr, &new_bytes, nullptr);

    // save_registry itself uses non-replacing rename semantics even on POSIX.
    TemporaryDirectory temporary;
    const auto root = temporary.path / "packages";
    PersonalPackageStore store(root.string());
    assert(store.save_registry(old_registry) == PackageStoreError::ok);
    assert(store.save_registry(new_registry) == PackageStoreError::ok);
    PackageRegistry loaded;
    assert(store.load_registry("nick.local", loaded) == PackageStoreError::ok);
    assert(loaded.find("dev.doodad.recovery")->current.semantic_version ==
           "2.0.0");
}

void verify_registry_and_store() {
    TemporaryDirectory temporary;
    const auto package_root = temporary.path / "packages";
    std::filesystem::create_directories(package_root / "incoming");
    PersonalPackageStore store(package_root.string());
    const auto abandoned_part = package_root / "incoming" /
        (std::string(64, 'c') + ".part");
    const auto unrelated_part = package_root / "incoming" / "notes.part";
    write_bytes(abandoned_part, {1, 2, 3});
    write_bytes(unrelated_part, {4, 5, 6});
    PackageRegistry registry;
    assert(store.load_registry("nick.local", registry) == PackageStoreError::ok);
    assert(!std::filesystem::exists(abandoned_part));
    assert(std::filesystem::exists(unrelated_part));
    assert(registry.apps().empty());
    assert(!registry.running().has_value());

    std::array<std::string, 3> generation_paths{};
    for (std::size_t index = 0; index < std::size(kFixtures); ++index) {
        const auto& fixture = kFixtures[index];
        const auto incoming = store.incoming_part_path(fixture.bundle_sha256);
        write_bytes(incoming, fixture_bundle(fixture));
        const auto installed = store.install_part(
            incoming, fixture.bundle_sha256, trust(), registry);
        assert(installed);
        assert(installed.installed_new_bytes);
        assert(installed.registry_changed);
        assert(!std::filesystem::exists(incoming));
        generation_paths[index] = installed.generation_path;
        const auto* app = registry.find("dev.doodad.rest-timer");
        assert(app != nullptr);
        assert(app->current.semantic_version == fixture.version);
        assert(app->current.payload_sha256 == fixture.payload_sha256);
        assert(std::filesystem::is_regular_file(
            store.wasm_path(app->current)));
        assert(std::filesystem::is_regular_file(
            std::filesystem::path(installed.generation_path) / "bundle.ddb"));
        std::ifstream wasm(store.wasm_path(app->current), std::ios::binary);
        const std::vector<std::uint8_t> extracted(
            (std::istreambuf_iterator<char>(wasm)),
            std::istreambuf_iterator<char>());
        assert(extracted == from_hex(fixture.payload_hex));
        if (index > 0) {
            assert(app->previous.has_value());
            assert(app->previous->semantic_version == kFixtures[index - 1].version);
        }
        if (index == 2) {
            assert(installed.generations_collected == 1);
            assert(!std::filesystem::exists(generation_paths[0]));
            assert(std::filesystem::exists(generation_paths[1]));
            assert(std::filesystem::exists(generation_paths[2]));
        }
    }

    // A reconnect replay of the retained previous generation is idempotent and
    // cannot silently downgrade current.
    const auto previous_incoming =
        store.incoming_part_path(kFixtures[1].bundle_sha256);
    write_bytes(previous_incoming, fixture_bundle(kFixtures[1]));
    const auto current_before_prepare =
        registry.find("dev.doodad.rest-timer")->current;
    auto replayed = store.prepare_part(
        previous_incoming, kFixtures[1].bundle_sha256, trust());
    assert(replayed);
    assert(!replayed.installed_new_bytes);
    assert(registry.find("dev.doodad.rest-timer")->current ==
           current_before_prepare);
    assert(store.commit_prepared(replayed, registry) == PackageStoreError::ok);
    std::size_t replay_cleanup = 0;
    assert(store.garbage_collect(registry, replay_cleanup) ==
           PackageStoreError::ok);
    assert(!replayed.registry_changed);
    assert(registry.find("dev.doodad.rest-timer")->current.semantic_version ==
           std::string(kFixtures[2].version));

    // A validly signed envelope that reuses the exact generation triple while
    // changing signed display metadata has the same storage path but a
    // different whole-bundle digest. It is a conflict, not a new generation.
    std::string alternate_bundle_sha256;
    const auto alternate_bundle = signed_fixture_bundle(
        kFixtures[2], "Lift Rest Renamed", alternate_bundle_sha256);
    assert(alternate_bundle_sha256 != kFixtures[2].bundle_sha256);
    const auto alternate_incoming =
        store.incoming_part_path(alternate_bundle_sha256);
    write_bytes(alternate_incoming, alternate_bundle);
    const auto before_conflict =
        registry.find("dev.doodad.rest-timer")->current;
    const auto conflicted = store.prepare_part(
        alternate_incoming, alternate_bundle_sha256, trust());
    assert(conflicted.error == PackageStoreError::generation_conflict);
    assert(conflicted.bundle_error == PersonalBundleError::ok);
    assert(registry.find("dev.doodad.rest-timer")->current == before_conflict);

    // Running is explicit and volatile. Rollback quarantines the failed current
    // generation, clears resident state, and cannot flip-flop into it again.
    assert(registry.mark_running(
        "dev.doodad.rest-timer",
        kFixtures[2].version,
        kFixtures[2].payload_sha256));
    assert(registry.running().has_value());
    assert(registry.rollback_eligible("dev.doodad.rest-timer"));
    assert(registry.roll_back(
        "dev.doodad.rest-timer",
        kFixtures[2].version,
        kFixtures[2].payload_sha256));
    assert(!registry.running().has_value());
    assert(registry.is_quarantined(
        "dev.doodad.rest-timer",
        kFixtures[2].version,
        kFixtures[2].payload_sha256));
    assert(!registry.rollback_eligible("dev.doodad.rest-timer"));
    assert(!registry.roll_back(
        "dev.doodad.rest-timer",
        kFixtures[1].version,
        kFixtures[1].payload_sha256));
    assert(store.save_registry(registry) == PackageStoreError::ok);

    const auto encoded = doodad::packages::encode_package_registry(registry);
    assert(encoded.size() > 4);
    assert(std::string(encoded.begin(), encoded.begin() + 4) == "DDR3");
    auto old_layout = encoded;
    old_layout[3] = '1';
    const auto old_checksum = doodad::packages::sha256_bytes(
        old_layout.data(), old_layout.size() - doodad::packages::kSha256Bytes);
    std::copy(
        old_checksum.begin(), old_checksum.end(),
        old_layout.end() - doodad::packages::kSha256Bytes);
    PackageRegistry rejected_old_layout;
    assert(!doodad::packages::decode_package_registry(
        old_layout.data(), old_layout.size(), rejected_old_layout));
    auto corrupt_registry = encoded;
    corrupt_registry.back() ^= 1;
    assert(!doodad::packages::decode_package_registry(
        corrupt_registry.data(), corrupt_registry.size(), rejected_old_layout));

    // Simulate power loss after a registry commit but before cleanup. Loading
    // the checksummed registry removes only unreferenced/staging generations.
    const auto orphan = package_root / "apps" / "dev.doodad.rest-timer" /
        std::string(64, 'f');
    const auto interrupted = package_root / "apps" /
        "dev.doodad.rest-timer" /
        (std::string(64, 'e') + ".installing");
    std::filesystem::create_directories(orphan);
    std::filesystem::create_directories(interrupted);
    write_bytes(orphan / "app.wasm", {1});
    write_bytes(orphan / "bundle.ddb", {2});
    write_bytes(interrupted / "app.wasm.part", {3});
    write_bytes(interrupted / "bundle.ddb", {4});

    PackageRegistry restored;
    assert(store.load_registry("nick.local", restored) == PackageStoreError::ok);
    assert(!std::filesystem::exists(orphan));
    assert(!std::filesystem::exists(interrupted));
    assert(std::filesystem::exists(generation_paths[1]));
    assert(std::filesystem::exists(generation_paths[2]));
    assert(!restored.running().has_value());
    assert(restored.find("dev.doodad.rest-timer")->current.semantic_version ==
           std::string(kFixtures[1].version));
    assert(restored.is_quarantined(
        "dev.doodad.rest-timer",
        kFixtures[2].version,
        kFixtures[2].payload_sha256));

    // Owner binding applies to the registry independently of bundle checks.
    PackageRegistry other_owner;
    assert(store.load_registry("somebody.else", other_owner) ==
           PackageStoreError::owner_mismatch);

    // A valid bundle outside the exact incoming .part path is not installable.
    const auto stray = package_root / "stray.part";
    write_bytes(stray, fixture_bundle(kFixtures[0]));
    const auto rejected = store.install_part(
        stray.string(), kFixtures[0].bundle_sha256, trust(), restored);
    assert(rejected.error == PackageStoreError::invalid_part_path);

    // A corrupted transport object cannot mutate current/previous state.
    const auto current_before =
        restored.find("dev.doodad.rest-timer")->current.payload_sha256;
    auto corrupt_bundle = fixture_bundle(kFixtures[0]);
    corrupt_bundle.back() ^= 1;
    const auto corrupt_part =
        store.incoming_part_path(kFixtures[0].bundle_sha256);
    write_bytes(corrupt_part, corrupt_bundle);
    const auto corrupt_install = store.install_part(
        corrupt_part, kFixtures[0].bundle_sha256, trust(), restored);
    assert(corrupt_install.error == PackageStoreError::bundle_verification);
    assert(restored.find("dev.doodad.rest-timer")->current.payload_sha256 ==
           current_before);

    // Launch loading re-audits the retained signed bundle and hashes the exact
    // caller-owned bytes. Corruption in either retained artifact fails closed.
    const auto* restored_app = restored.find("dev.doodad.rest-timer");
    assert(restored_app != nullptr);
    const auto generation = restored_app->current;
    std::vector<std::uint8_t> loaded;
    assert(store.load_verified_wasm(
        restored_app->app_id, generation, trust(), 1024 * 1024, loaded));
    assert(loaded == from_hex(kFixtures[1].payload_hex));
    auto wrong_name_generation = generation;
    wrong_name_generation.name = "Wrong signed name";
    assert(!store.load_verified_wasm(
        restored_app->app_id,
        wrong_name_generation,
        trust(),
        1024 * 1024,
        loaded));
    assert(loaded.empty());
    const auto bundle_path = std::filesystem::path(package_root) /
        generation.relative_path / "bundle.ddb";
    auto retained_bundle = read_bytes(bundle_path);
    retained_bundle.back() ^= 1;
    write_bytes(bundle_path, retained_bundle);
    loaded = {1, 2, 3};
    assert(!store.load_verified_wasm(
        restored_app->app_id, generation, trust(), 1024 * 1024, loaded));
    assert(loaded.empty());
    write_bytes(bundle_path, fixture_bundle(kFixtures[1]));
    write_bytes(store.wasm_path(generation), {0, 1, 2, 3});
    loaded = {1, 2, 3};
    assert(!store.load_verified_wasm(
        restored_app->app_id, generation, trust(), 1024 * 1024, loaded));
    assert(loaded.empty());
}

}  // namespace

int main() {
    verify_bundle_contract();
    verify_manifest_bounds();
    verify_generation_identity_selection();
    verify_noncurrent_failure_quarantine();
    verify_quarantine_history();
    verify_quarantine_capacity_and_canonical_encoding();
    verify_registry_promotion_recovery();
    verify_registry_and_store();
    return 0;
}

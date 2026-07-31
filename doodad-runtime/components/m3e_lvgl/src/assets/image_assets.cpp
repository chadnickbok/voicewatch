#include "m3e/assets/image_assets.hpp"

#include <cstring>

extern "C" {
extern const char doodad_media_art_sha256[];
extern const std::uint8_t doodad_media_art_dimg[];
extern const std::size_t doodad_media_art_dimg_size;
extern const char doodad_wallet_qr_sha256[];
extern const std::uint8_t doodad_wallet_qr_dimg[];
extern const std::size_t doodad_wallet_qr_dimg_size;
}

namespace m3e {
namespace {

constexpr std::size_t kHeaderBytes = 12;
constexpr std::uint8_t kRgb565LittleEndian = 1;

struct EmbeddedAsset {
    const char* sha256;
    const std::uint8_t* payload;
    std::size_t payload_size;
};

const EmbeddedAsset kAssets[] = {
    {
        doodad_media_art_sha256,
        doodad_media_art_dimg,
        doodad_media_art_dimg_size,
    },
    {
        doodad_wallet_qr_sha256,
        doodad_wallet_qr_dimg,
        doodad_wallet_qr_dimg_size,
    },
};

std::uint16_t little_endian_u16(const std::uint8_t* bytes) {
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[0]) |
        static_cast<std::uint16_t>(bytes[1]) << 8U);
}

}  // namespace

bool resolve_image_asset(
    const char* sha256,
    ImageAssetView& output) {
    output = {};
    if (sha256 == nullptr) {
        return false;
    }
    const EmbeddedAsset* match = nullptr;
    for (const auto& asset : kAssets) {
        if (std::strcmp(sha256, asset.sha256) == 0) {
            match = &asset;
            break;
        }
    }
    if (match == nullptr || match->payload_size < kHeaderBytes) {
        return false;
    }
    const auto* payload = match->payload;
    if (std::memcmp(payload, "DIMG", 4) != 0 ||
        payload[8] != kRgb565LittleEndian ||
        payload[9] != 0 ||
        little_endian_u16(payload + 10) != 0) {
        return false;
    }
    const auto width = little_endian_u16(payload + 4);
    const auto height = little_endian_u16(payload + 6);
    const auto decoded_bytes =
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) * 2U;
    if (width == 0 || height == 0 ||
        match->payload_size != kHeaderBytes + decoded_bytes) {
        return false;
    }
    output = {
        payload + kHeaderBytes,
        decoded_bytes,
        width,
        height,
    };
    return true;
}

}  // namespace m3e

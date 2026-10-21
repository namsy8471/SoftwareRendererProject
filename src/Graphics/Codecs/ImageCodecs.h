#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// 자체 이미지 코덱. 외부 라이브러리 없이 메모리 버퍼만 다루며 파일·Texture를 모른다.
// 모든 디코더는 원본 채널 수와 관계없이 RGBA8(위에서 아래 행 순서, stride == width * 4)을 만든다.
namespace sr::codec
{
struct DecodedImage
{
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

enum class ImageFormat
{
    Unknown,
    Png,
    Jpeg,
    Bmp,
    Tga,
    Dds,
};

// 손상된 헤더가 거대한 할당을 일으키지 않도록 픽셀 수를 제한한다(8192×8192 = 256 MiB RGBA).
inline constexpr std::size_t maxDecodedPixels = std::size_t{8192} * 8192;

// PNG·JPEG·BMP·DDS는 시그니처로, 시그니처가 없는 TGA는 확장자(".tga", 대소문자 무시)로 판별한다.
[[nodiscard]] ImageFormat DetectImageFormat(std::span<const std::uint8_t> bytes,
                                            std::string_view extension) noexcept;
[[nodiscard]] std::expected<DecodedImage, std::string> DecodeImage(std::span<const std::uint8_t> bytes,
                                                                   std::string_view extension = {});

// PNG: 색 형식 0/2/3/4/6, 비트 깊이 1~16, Adam7 인터레이스, tRNS. 16비트는 상위 8비트를 쓴다.
[[nodiscard]] std::expected<DecodedImage, std::string> DecodePng(std::span<const std::uint8_t> bytes);
// JPEG: 베이스라인(SOF0/SOF1) 허프만, 1·3 성분, 임의 샘플링 비율, 재시작 마커. 프로그레시브는 오류다.
[[nodiscard]] std::expected<DecodedImage, std::string> DecodeJpeg(std::span<const std::uint8_t> bytes);
// BMP: 1/4/8/16/24/32비트, BI_RGB·BI_BITFIELDS, 위·아래 방향 행. RLE 압축은 오류다.
[[nodiscard]] std::expected<DecodedImage, std::string> DecodeBmp(std::span<const std::uint8_t> bytes);
// TGA: 팔레트·트루컬러·회색(형식 1/2/3)과 RLE(9/10/11), 8/15/16/24/32비트.
[[nodiscard]] std::expected<DecodedImage, std::string> DecodeTga(std::span<const std::uint8_t> bytes);

// DDS: 첫 면·첫 밉. 비압축 마스크 형식(8~32비트)과 DX10 RGBA8/BGRA8, BC1~BC5·BC7. BC6H·부호 있는 BC4/5는 오류다.
[[nodiscard]] std::expected<DecodedImage, std::string> DecodeDds(std::span<const std::uint8_t> bytes);

// zlib 스트림(RFC 1950)을 풀어 DEFLATE(RFC 1951) 결과를 돌려준다. Adler-32도 확인한다.
// expectedSize는 예약 크기 힌트이고, 출력이 maxOutput을 넘으면 오류다(압축 폭탄 방지).
[[nodiscard]] std::expected<std::vector<std::uint8_t>, std::string> InflateZlib(
    std::span<const std::uint8_t> stream, std::size_t expectedSize, std::size_t maxOutput);

// 8-bit RGB PNG를 만든다(행 필터 + 고정 허프만 DEFLATE). 골든 이미지·srcli 출력용이다.
[[nodiscard]] std::vector<std::uint8_t> EncodePngRgb(int width, int height,
                                                     std::span<const std::uint8_t> rgb);
} // namespace sr::codec

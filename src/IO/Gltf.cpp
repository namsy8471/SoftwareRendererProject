#include "IO/Gltf.h"
#include "IO/FileIO.h"
#include "IO/Json.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace sr::gltf
{
namespace
{
using json::Value;
using Error = std::string;
template <class T> using Result = std::expected<T, Error>;

// 지원하는 필수 확장. 양자화된 정점은 ReadAccessorFloats가 그대로 처리한다.
constexpr std::array supportedRequiredExtensions{std::string_view{"KHR_mesh_quantization"}};
// bufferView 없는(전부 0인) accessor가 만들 수 있는 최대 바이트. 큰 count로 메모리를 다 쓰는 입력을 막는다.
constexpr std::uint64_t maxZeroAccessorBytes = 1ull << 28;
constexpr std::uint32_t glbMagic = 0x46546C67; // "glTF"
constexpr std::uint32_t glbChunkJson = 0x4E4F534A;
constexpr std::uint32_t glbChunkBin = 0x004E4942;

std::string where(std::string_view array, std::size_t index)
{
    return std::string(array) + "[" + std::to_string(index) + "]";
}

// --- JSON 값 읽기: 타입이 다르면 위치가 담긴 오류를 돌려준다 ---

Result<const Value::Array *> optionalArray(const Value &object, std::string_view key,
                                           std::string_view context)
{
    const Value *value = object.Find(key);
    if (!value)
        return nullptr;
    if (const auto *array = value->AsArray())
        return array;
    return std::unexpected(std::string(context) + "." + std::string(key) + " must be an array");
}

Result<const Value *> optionalObject(const Value &object, std::string_view key, std::string_view context)
{
    const Value *value = object.Find(key);
    if (!value)
        return nullptr;
    if (value->AsObject())
        return value;
    return std::unexpected(std::string(context) + "." + std::string(key) + " must be an object");
}

// 0 이상 정수. JSON 숫자는 double이므로 2^53까지만 정확하다.
Result<std::uint64_t> toUnsigned(const Value &value, std::string_view name)
{
    const double *number = value.AsNumber();
    constexpr double exactLimit = 9007199254740992.0;
    if (!number || *number < 0 || *number > exactLimit || std::floor(*number) != *number)
        return std::unexpected(std::string(name) + " must be a non-negative integer");
    return static_cast<std::uint64_t>(*number);
}

Result<std::uint64_t> readUnsigned(const Value &object, std::string_view key, std::string_view context,
                                   std::optional<std::uint64_t> fallback)
{
    const std::string name = std::string(context) + "." + std::string(key);
    const Value *value = object.Find(key);
    if (!value)
    {
        if (fallback)
            return *fallback;
        return std::unexpected(name + " is required");
    }
    return toUnsigned(*value, name);
}

Result<std::size_t> readSize(const Value &object, std::string_view key, std::string_view context,
                             std::optional<std::uint64_t> fallback = 0)
{
    auto value = readUnsigned(object, key, context, fallback);
    if (!value)
        return std::unexpected(value.error());
    if (*value > std::numeric_limits<std::size_t>::max())
        return std::unexpected(std::string(context) + "." + std::string(key) + " is too large");
    return static_cast<std::size_t>(*value);
}

// 다른 배열을 가리키는 인덱스. 범위는 모든 배열을 읽은 뒤 validateIndex로 검사한다.
Result<int> readIndex(const Value &object, std::string_view key, std::string_view context,
                      bool required = false)
{
    const Value *value = object.Find(key);
    if (!value)
    {
        if (required)
            return std::unexpected(std::string(context) + "." + std::string(key) + " is required");
        return none;
    }
    auto index = toUnsigned(*value, std::string(context) + "." + std::string(key));
    if (!index)
        return std::unexpected(index.error());
    if (*index > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
        return std::unexpected(std::string(context) + "." + std::string(key) + " is too large");
    return static_cast<int>(*index);
}

Result<std::string> readString(const Value &object, std::string_view key, std::string_view context)
{
    const Value *value = object.Find(key);
    if (!value)
        return std::string{};
    if (const auto *text = value->AsString())
        return *text;
    return std::unexpected(std::string(context) + "." + std::string(key) + " must be a string");
}

Result<float> readFloat(const Value &object, std::string_view key, std::string_view context, float fallback)
{
    const Value *value = object.Find(key);
    if (!value)
        return fallback;
    const double *number = value->AsNumber();
    if (!number || !std::isfinite(static_cast<float>(*number)))
        return std::unexpected(std::string(context) + "." + std::string(key) + " must be a finite number");
    return static_cast<float>(*number);
}

Result<bool> readBool(const Value &object, std::string_view key, std::string_view context, bool fallback)
{
    const Value *value = object.Find(key);
    if (!value)
        return fallback;
    if (const bool *flag = value->AsBool())
        return *flag;
    return std::unexpected(std::string(context) + "." + std::string(key) + " must be a boolean");
}

template <std::size_t N>
Result<std::array<float, N>> readFloats(const Value &object, std::string_view key, std::string_view context,
                                        const std::array<float, N> &fallback)
{
    auto array = optionalArray(object, key, context);
    if (!array)
        return std::unexpected(array.error());
    if (!*array)
        return fallback;
    const std::string name = std::string(context) + "." + std::string(key);
    if ((*array)->size() != N)
        return std::unexpected(name + " must have " + std::to_string(N) + " numbers");
    std::array<float, N> result{};
    for (std::size_t i = 0; i < N; ++i)
    {
        const double *number = (**array)[i].AsNumber();
        if (!number || !std::isfinite(static_cast<float>(*number)))
            return std::unexpected(name + " must contain finite numbers");
        result[i] = static_cast<float>(*number);
    }
    return result;
}

Result<std::vector<int>> readIndices(const Value &object, std::string_view key, std::string_view context)
{
    auto array = optionalArray(object, key, context);
    if (!array)
        return std::unexpected(array.error());
    std::vector<int> result;
    if (!*array)
        return result;
    result.reserve((*array)->size());
    for (std::size_t i = 0; i < (*array)->size(); ++i)
    {
        auto index = toUnsigned((**array)[i], std::string(context) + "." + std::string(key));
        if (!index)
            return std::unexpected(index.error());
        if (*index > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            return std::unexpected(std::string(context) + "." + std::string(key) + " is too large");
        result.push_back(static_cast<int>(*index));
    }
    return result;
}

// 최상위 배열의 각 원소가 객체인지 확인하고 parse(object, context)로 넘긴다.
template <class T, class Parse>
Result<std::vector<T>> readObjects(const Value &root, std::string_view key, Parse parse)
{
    auto array = optionalArray(root, key, "glTF");
    if (!array)
        return std::unexpected(array.error());
    std::vector<T> result;
    if (!*array)
        return result;
    result.reserve((*array)->size());
    for (std::size_t i = 0; i < (*array)->size(); ++i)
    {
        const std::string context = where(key, i);
        const Value &item = (**array)[i];
        if (!item.AsObject())
            return std::unexpected(context + " must be an object");
        auto parsed = parse(item, context);
        if (!parsed)
            return std::unexpected(parsed.error());
        result.push_back(std::move(*parsed));
    }
    return result;
}

Result<void> validateIndex(int index, std::size_t size, std::string_view context, std::string_view target)
{
    if (index == none || static_cast<std::size_t>(index) < size)
        return {};
    return std::unexpected(std::string(context) + " refers to missing " + std::string(target) + " " +
                           std::to_string(index));
}

// --- 바이트 단위 도구 ---

std::size_t componentSize(ComponentType type) noexcept
{
    switch (type)
    {
    case ComponentType::Byte:
    case ComponentType::UnsignedByte:
        return 1;
    case ComponentType::Short:
    case ComponentType::UnsignedShort:
        return 2;
    case ComponentType::UnsignedInt:
    case ComponentType::Float:
        return 4;
    }
    return 0;
}

std::optional<ComponentType> toComponentType(std::uint64_t value) noexcept
{
    switch (value)
    {
    case 5120:
    case 5121:
    case 5122:
    case 5123:
    case 5125:
    case 5126:
        return static_cast<ComponentType>(value);
    default:
        return std::nullopt;
    }
}

int componentCountOf(std::string_view type) noexcept
{
    if (type == "SCALAR")
        return 1;
    if (type == "VEC2")
        return 2;
    if (type == "VEC3")
        return 3;
    if (type == "VEC4" || type == "MAT2")
        return 4;
    if (type == "MAT3")
        return 9;
    if (type == "MAT4")
        return 16;
    return 0;
}

// 리틀 엔디언 파일 값을 읽는다. 정렬을 가정하지 않도록 memcpy를 쓴다(x86/x64 대상).
template <class T> T loadLittle(const std::byte *bytes) noexcept
{
    static_assert(std::endian::native == std::endian::little);
    T value;
    std::memcpy(&value, bytes, sizeof(T));
    return value;
}

float loadComponentAsFloat(const std::byte *bytes, ComponentType type, bool normalized) noexcept
{
    switch (type)
    {
    case ComponentType::Byte:
    {
        const float value = static_cast<float>(loadLittle<std::int8_t>(bytes));
        return normalized ? std::max(value / 127.f, -1.f) : value;
    }
    case ComponentType::UnsignedByte:
    {
        const float value = static_cast<float>(loadLittle<std::uint8_t>(bytes));
        return normalized ? value / 255.f : value;
    }
    case ComponentType::Short:
    {
        const float value = static_cast<float>(loadLittle<std::int16_t>(bytes));
        return normalized ? std::max(value / 32767.f, -1.f) : value;
    }
    case ComponentType::UnsignedShort:
    {
        const float value = static_cast<float>(loadLittle<std::uint16_t>(bytes));
        return normalized ? value / 65535.f : value;
    }
    case ComponentType::UnsignedInt:
    {
        const auto value = static_cast<float>(loadLittle<std::uint32_t>(bytes));
        return normalized ? value / 4294967295.f : value;
    }
    case ComponentType::Float:
        return loadLittle<float>(bytes);
    }
    return 0.f;
}

std::optional<std::uint32_t> loadUnsigned(const std::byte *bytes, ComponentType type) noexcept
{
    switch (type)
    {
    case ComponentType::UnsignedByte:
        return loadLittle<std::uint8_t>(bytes);
    case ComponentType::UnsignedShort:
        return loadLittle<std::uint16_t>(bytes);
    case ComponentType::UnsignedInt:
        return loadLittle<std::uint32_t>(bytes);
    default:
        return std::nullopt;
    }
}

// offset + size <= limit 를 넘침 없이 검사한다.
bool fitsIn(std::uint64_t offset, std::uint64_t size, std::uint64_t limit) noexcept
{
    return offset <= limit && size <= limit - offset;
}

// stride 간격 count개 요소(각 elementSize바이트)가 차지하는 바이트. 넘치면 nullopt.
std::optional<std::uint64_t> stridedSpan(std::uint64_t count, std::uint64_t stride,
                                         std::uint64_t elementSize) noexcept
{
    if (count == 0)
        return 0;
    if (stride != 0 && count - 1 > (std::numeric_limits<std::uint64_t>::max() - elementSize) / stride)
        return std::nullopt;
    return (count - 1) * stride + elementSize;
}

int hexValue(char c) noexcept
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// URI의 %XX를 푼다(공백이 든 파일 이름은 %20으로 저장된다).
std::optional<std::string> percentDecode(std::string_view uri)
{
    std::string result;
    result.reserve(uri.size());
    for (std::size_t i = 0; i < uri.size(); ++i)
    {
        if (uri[i] != '%')
        {
            result.push_back(uri[i]);
            continue;
        }
        if (i + 2 >= uri.size())
            return std::nullopt;
        const int high = hexValue(uri[i + 1]);
        const int low = hexValue(uri[i + 2]);
        if (high < 0 || low < 0)
            return std::nullopt;
        result.push_back(static_cast<char>(high * 16 + low));
        i += 2;
    }
    return result;
}

// data URI 또는 baseDirectory 기준 상대 경로를 읽는다. http: 같은 스킴과 절대 경로는 거부한다.
std::expected<std::vector<std::byte>, AssetLoadError> readUri(std::string_view uri,
                                                              const std::filesystem::path &baseDirectory,
                                                              std::uintmax_t maxBytes,
                                                              std::string_view context)
{
    if (uri.starts_with("data:"))
    {
        const auto marker = uri.find(";base64,");
        if (marker == std::string_view::npos)
            return std::unexpected(
                AssetLoadError{std::string(context) + ": only base64 data URIs are supported"});
        auto decoded = DecodeBase64(uri.substr(marker + 8));
        if (!decoded)
            return std::unexpected(AssetLoadError{std::string(context) + ": invalid base64 data URI"});
        return std::move(*decoded);
    }
    const auto colon = uri.find(':');
    const auto slash = uri.find('/');
    if ((colon != std::string_view::npos && (slash == std::string_view::npos || colon < slash)) ||
        uri.starts_with('/') || uri.starts_with('\\'))
        return std::unexpected(
            AssetLoadError{std::string(context) + ": only relative file URIs and data URIs are supported"});
    const auto decoded = percentDecode(uri);
    if (!decoded || decoded->empty())
        return std::unexpected(AssetLoadError{std::string(context) + ": invalid URI"});
    // URI는 UTF-8이다. Windows에서 path(std::string)은 ANSI 코드 페이지로 해석하므로 u8string을 거친다.
    const std::u8string utf8(reinterpret_cast<const char8_t *>(decoded->data()), decoded->size());
    const auto path = baseDirectory / std::filesystem::path(utf8);
    auto bytes = io::ReadWholeFile(path, maxBytes);
    if (!bytes)
        return std::unexpected(std::move(bytes.error()));
    std::vector<std::byte> result(bytes->size());
    std::memcpy(result.data(), bytes->data(), bytes->size());
    return result;
}

// --- 항목별 파서 ---

Result<BufferView> parseBufferView(const Value &item, const std::string &context)
{
    BufferView view;
    auto buffer = readIndex(item, "buffer", context, true);
    auto offset = readSize(item, "byteOffset", context);
    auto length = readSize(item, "byteLength", context, std::nullopt);
    auto stride = readSize(item, "byteStride", context);
    if (!buffer)
        return std::unexpected(buffer.error());
    if (!offset)
        return std::unexpected(offset.error());
    if (!length)
        return std::unexpected(length.error());
    if (!stride)
        return std::unexpected(stride.error());
    if (*stride != 0 && (*stride < 4 || *stride > 252 || *stride % 4 != 0))
        return std::unexpected(context + ".byteStride must be a multiple of 4 in [4, 252]");
    view.buffer = *buffer;
    view.byteOffset = *offset;
    view.byteLength = *length;
    view.byteStride = *stride;
    return view;
}

Result<ComponentType> readComponentType(const Value &item, const std::string &context)
{
    auto raw = readUnsigned(item, "componentType", context, std::nullopt);
    if (!raw)
        return std::unexpected(raw.error());
    const auto type = toComponentType(*raw);
    if (!type)
        return std::unexpected(context + ".componentType " + std::to_string(*raw) + " is not valid");
    return *type;
}

Result<Accessor> parseAccessor(const Value &item, const std::string &context)
{
    Accessor accessor;
    auto view = readIndex(item, "bufferView", context);
    auto offset = readSize(item, "byteOffset", context);
    auto type = readComponentType(item, context);
    auto normalized = readBool(item, "normalized", context, false);
    auto count = readSize(item, "count", context, std::nullopt);
    auto typeName = readString(item, "type", context);
    if (!view)
        return std::unexpected(view.error());
    if (!offset)
        return std::unexpected(offset.error());
    if (!type)
        return std::unexpected(type.error());
    if (!normalized)
        return std::unexpected(normalized.error());
    if (!count)
        return std::unexpected(count.error());
    if (!typeName)
        return std::unexpected(typeName.error());
    accessor.bufferView = *view;
    accessor.byteOffset = *offset;
    accessor.componentType = *type;
    accessor.normalized = *normalized;
    accessor.count = *count;
    accessor.componentCount = componentCountOf(*typeName);
    if (accessor.count == 0)
        return std::unexpected(context + ".count must be at least 1");
    if (accessor.componentCount == 0)
        return std::unexpected(context + ".type '" + *typeName + "' is not valid");
    // MAT2/MAT3의 1·2바이트 성분은 열마다 4바이트 정렬 패딩이 들어간다. 실제 파일에서 거의 쓰지 않아
    // 거부한다.
    if ((*typeName == "MAT2" || *typeName == "MAT3") && accessor.componentType != ComponentType::Float &&
        !(*typeName == "MAT2" && componentSize(accessor.componentType) == 2))
        return std::unexpected(context + ": padded matrix accessors are not supported");
    if (accessor.normalized && (accessor.componentType == ComponentType::Float ||
                                accessor.componentType == ComponentType::UnsignedInt))
        return std::unexpected(context + ".normalized is only valid for 8/16-bit integers");

    auto sparse = optionalObject(item, "sparse", context);
    if (!sparse)
        return std::unexpected(sparse.error());
    if (*sparse)
    {
        const std::string sparseContext = context + ".sparse";
        AccessorSparse result;
        auto sparseCount = readSize(**sparse, "count", sparseContext, std::nullopt);
        auto indices = optionalObject(**sparse, "indices", sparseContext);
        auto values = optionalObject(**sparse, "values", sparseContext);
        if (!sparseCount)
            return std::unexpected(sparseCount.error());
        if (!indices)
            return std::unexpected(indices.error());
        if (!values)
            return std::unexpected(values.error());
        if (!*indices || !*values)
            return std::unexpected(sparseContext + " needs indices and values");
        if (*sparseCount == 0 || *sparseCount > accessor.count)
            return std::unexpected(sparseContext + ".count must be in [1, accessor count]");
        result.count = *sparseCount;
        const std::string indicesContext = sparseContext + ".indices";
        auto indexView = readIndex(**indices, "bufferView", indicesContext, true);
        auto indexOffset = readSize(**indices, "byteOffset", indicesContext);
        auto indexType = readComponentType(**indices, indicesContext);
        const std::string valuesContext = sparseContext + ".values";
        auto valueView = readIndex(**values, "bufferView", valuesContext, true);
        auto valueOffset = readSize(**values, "byteOffset", valuesContext);
        if (!indexView)
            return std::unexpected(indexView.error());
        if (!indexOffset)
            return std::unexpected(indexOffset.error());
        if (!indexType)
            return std::unexpected(indexType.error());
        if (!valueView)
            return std::unexpected(valueView.error());
        if (!valueOffset)
            return std::unexpected(valueOffset.error());
        if (*indexType == ComponentType::Byte || *indexType == ComponentType::Short ||
            *indexType == ComponentType::Float)
            return std::unexpected(indicesContext + ".componentType must be an unsigned integer");
        result.indicesBufferView = *indexView;
        result.indicesByteOffset = *indexOffset;
        result.indicesComponentType = *indexType;
        result.valuesBufferView = *valueView;
        result.valuesByteOffset = *valueOffset;
        accessor.sparse = result;
    }
    return accessor;
}

std::optional<PrimitiveMode> toPrimitiveMode(std::uint64_t value) noexcept
{
    if (value > 6)
        return std::nullopt;
    return static_cast<PrimitiveMode>(value);
}

Result<Mesh> parseMesh(const Value &item, const std::string &context)
{
    Mesh mesh;
    auto name = readString(item, "name", context);
    if (!name)
        return std::unexpected(name.error());
    mesh.name = std::move(*name);
    auto primitives = optionalArray(item, "primitives", context);
    if (!primitives)
        return std::unexpected(primitives.error());
    if (!*primitives || (*primitives)->empty())
        return std::unexpected(context + ".primitives must not be empty");
    for (std::size_t i = 0; i < (*primitives)->size(); ++i)
    {
        const Value &value = (**primitives)[i];
        const std::string primitiveContext = context + "." + where("primitives", i);
        if (!value.AsObject())
            return std::unexpected(primitiveContext + " must be an object");
        Primitive primitive;
        auto attributes = optionalObject(value, "attributes", primitiveContext);
        if (!attributes)
            return std::unexpected(attributes.error());
        if (!*attributes)
            return std::unexpected(primitiveContext + ".attributes is required");
        for (const auto &member : *(*attributes)->AsObject())
        {
            auto accessor = toUnsigned(member.value, primitiveContext + ".attributes." + member.key);
            if (!accessor)
                return std::unexpected(accessor.error());
            if (*accessor > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
                return std::unexpected(primitiveContext + ".attributes." + member.key + " is too large");
            primitive.attributes.push_back({member.key, static_cast<int>(*accessor)});
        }
        auto indices = readIndex(value, "indices", primitiveContext);
        auto material = readIndex(value, "material", primitiveContext);
        auto mode = readUnsigned(value, "mode", primitiveContext, 4);
        auto targets = optionalArray(value, "targets", primitiveContext);
        if (!indices)
            return std::unexpected(indices.error());
        if (!material)
            return std::unexpected(material.error());
        if (!mode)
            return std::unexpected(mode.error());
        if (!targets)
            return std::unexpected(targets.error());
        const auto primitiveMode = toPrimitiveMode(*mode);
        if (!primitiveMode)
            return std::unexpected(primitiveContext + ".mode " + std::to_string(*mode) + " is not valid");
        primitive.indices = *indices;
        primitive.material = *material;
        primitive.mode = *primitiveMode;
        primitive.hasMorphTargets = *targets && !(*targets)->empty();
        mesh.primitives.push_back(std::move(primitive));
    }
    return mesh;
}

Result<TextureRef> parseTextureRef(const Value &object, std::string_view key, const std::string &context)
{
    auto info = optionalObject(object, key, context);
    if (!info)
        return std::unexpected(info.error());
    if (!*info)
        return TextureRef{};
    const std::string infoContext = context + "." + std::string(key);
    auto index = readIndex(**info, "index", infoContext, true);
    auto texCoord = readSize(**info, "texCoord", infoContext);
    if (!index)
        return std::unexpected(index.error());
    if (!texCoord)
        return std::unexpected(texCoord.error());
    if (*texCoord > 255)
        return std::unexpected(infoContext + ".texCoord is too large");
    return TextureRef{*index, static_cast<int>(*texCoord)};
}

Result<Material> parseMaterial(const Value &item, const std::string &context)
{
    Material material;
    auto name = readString(item, "name", context);
    auto emissive = readFloats<3>(item, "emissiveFactor", context, material.emissiveFactor);
    auto doubleSided = readBool(item, "doubleSided", context, false);
    auto pbr = optionalObject(item, "pbrMetallicRoughness", context);
    auto emissiveTexture = parseTextureRef(item, "emissiveTexture", context);
    auto normalTexture = parseTextureRef(item, "normalTexture", context);
    if (!emissiveTexture)
        return std::unexpected(emissiveTexture.error());
    if (!normalTexture)
        return std::unexpected(normalTexture.error());
    if (!name)
        return std::unexpected(name.error());
    if (!emissive)
        return std::unexpected(emissive.error());
    if (!doubleSided)
        return std::unexpected(doubleSided.error());
    if (!pbr)
        return std::unexpected(pbr.error());
    material.name = std::move(*name);
    material.emissiveFactor = *emissive;
    material.emissiveTexture = *emissiveTexture;
    material.normalTexture = *normalTexture;
    material.doubleSided = *doubleSided;
    auto alphaModeStr = readString(item, "alphaMode", context);
    if (!alphaModeStr)
        return std::unexpected(alphaModeStr.error());
    if (!alphaModeStr->empty())
    {
        if (*alphaModeStr == "OPAQUE")
            material.alphaMode = 0;
        else if (*alphaModeStr == "MASK")
            material.alphaMode = 1;
        else if (*alphaModeStr == "BLEND")
            material.alphaMode = 2;
        else
            return std::unexpected(context + ".alphaMode is invalid: " + *alphaModeStr);
    }
    else
    {
        material.alphaMode = 0;
    }
    auto alphaCutoff = readFloat(item, "alphaCutoff", context, 0.5f);
    if (!alphaCutoff)
        return std::unexpected(alphaCutoff.error());
    material.alphaCutoff = *alphaCutoff;
    if (auto normalInfo = optionalObject(item, "normalTexture", context); !normalInfo)
        return std::unexpected(normalInfo.error());
    else if (*normalInfo)
    {
        auto scale = readFloat(**normalInfo, "scale", context + ".normalTexture", 1.f);
        if (!scale)
            return std::unexpected(scale.error());
        material.normalScale = *scale;
    }
    auto occTexture = parseTextureRef(item, "occlusionTexture", context);
    if (!occTexture)
        return std::unexpected(occTexture.error());
    material.occlusionTexture = *occTexture;
    if (auto occInfo = optionalObject(item, "occlusionTexture", context); !occInfo)
        return std::unexpected(occInfo.error());
    else if (*occInfo)
    {
        auto strength = readFloat(**occInfo, "strength", context + ".occlusionTexture", 1.f);
        if (!strength)
            return std::unexpected(strength.error());
        material.occlusionStrength = *strength;
    }
    if (*pbr)
    {
        const std::string pbrContext = context + ".pbrMetallicRoughness";
        auto baseColor = readFloats<4>(**pbr, "baseColorFactor", pbrContext, material.baseColorFactor);
        auto baseTexture = parseTextureRef(**pbr, "baseColorTexture", pbrContext);
        auto metallic = readFloat(**pbr, "metallicFactor", pbrContext, 1.f);
        auto roughness = readFloat(**pbr, "roughnessFactor", pbrContext, 1.f);
        auto mrTexture = parseTextureRef(**pbr, "metallicRoughnessTexture", pbrContext);
        if (!mrTexture)
            return std::unexpected(mrTexture.error());
        material.metallicRoughnessTexture = *mrTexture;
        if (!baseColor)
            return std::unexpected(baseColor.error());
        if (!baseTexture)
            return std::unexpected(baseTexture.error());
        if (!metallic)
            return std::unexpected(metallic.error());
        if (!roughness)
            return std::unexpected(roughness.error());
        material.baseColorFactor = *baseColor;
        material.baseColorTexture = *baseTexture;
        material.metallicFactor = std::clamp(*metallic, 0.f, 1.f);
        material.roughnessFactor = std::clamp(*roughness, 0.f, 1.f);
    }
    return material;
}

Result<Image> parseImage(const Value &item, const std::string &context)
{
    Image image;
    auto name = readString(item, "name", context);
    auto uri = readString(item, "uri", context);
    auto view = readIndex(item, "bufferView", context);
    auto mime = readString(item, "mimeType", context);
    if (!name)
        return std::unexpected(name.error());
    if (!uri)
        return std::unexpected(uri.error());
    if (!view)
        return std::unexpected(view.error());
    if (!mime)
        return std::unexpected(mime.error());
    if (uri->empty() == (*view == none))
        return std::unexpected(context + " needs exactly one of uri or bufferView");
    if (*view != none && mime->empty())
        return std::unexpected(context + ".mimeType is required with bufferView");
    image.name = std::move(*name);
    image.uri = std::move(*uri);
    image.bufferView = *view;
    image.mimeType = std::move(*mime);
    return image;
}

Result<Texture> parseTexture(const Value &item, const std::string &context)
{
    auto source = readIndex(item, "source", context);
    auto sampler = readIndex(item, "sampler", context);
    if (!source)
        return std::unexpected(source.error());
    if (!sampler)
        return std::unexpected(sampler.error());
    return Texture{*source, *sampler};
}

Result<Sampler> parseSampler(const Value &item, const std::string &context)
{
    Sampler sampler;
    for (auto [key, target] : {std::pair{"wrapS", &sampler.wrapS}, std::pair{"wrapT", &sampler.wrapT}})
    {
        auto mode = readUnsigned(item, key, context, 10497);
        if (!mode)
            return std::unexpected(mode.error());
        if (*mode != 33071 && *mode != 33648 && *mode != 10497)
            return std::unexpected(context + "." + key + " " + std::to_string(*mode) + " is not valid");
        *target = static_cast<int>(*mode);
    }
    return sampler;
}

Result<Node> parseNode(const Value &item, const std::string &context)
{
    Node node;
    auto name = readString(item, "name", context);
    auto children = readIndices(item, "children", context);
    auto mesh = readIndex(item, "mesh", context);
    auto skin = readIndex(item, "skin", context);
    auto translation = readFloats<3>(item, "translation", context, node.translation);
    auto rotation = readFloats<4>(item, "rotation", context, node.rotation);
    auto scale = readFloats<3>(item, "scale", context, node.scale);
    if (!name)
        return std::unexpected(name.error());
    if (!children)
        return std::unexpected(children.error());
    if (!mesh)
        return std::unexpected(mesh.error());
    if (!skin)
        return std::unexpected(skin.error());
    if (!translation)
        return std::unexpected(translation.error());
    if (!rotation)
        return std::unexpected(rotation.error());
    if (!scale)
        return std::unexpected(scale.error());
    node.name = std::move(*name);
    node.children = std::move(*children);
    node.mesh = *mesh;
    node.skin = *skin;
    node.translation = *translation;
    node.rotation = *rotation;
    node.scale = *scale;
    if (item.Find("matrix"))
    {
        auto matrix = readFloats<16>(item, "matrix", context, {});
        if (!matrix)
            return std::unexpected(matrix.error());
        node.matrix = *matrix;
    }
    return node;
}

Result<Scene> parseScene(const Value &item, const std::string &context)
{
    auto name = readString(item, "name", context);
    auto nodes = readIndices(item, "nodes", context);
    if (!name)
        return std::unexpected(name.error());
    if (!nodes)
        return std::unexpected(nodes.error());
    return Scene{std::move(*name), std::move(*nodes)};
}

Result<Skin> parseSkin(const Value &item, const std::string &context)
{
    auto name = readString(item, "name", context);
    auto inverseBind = readIndex(item, "inverseBindMatrices", context);
    auto skeleton = readIndex(item, "skeleton", context);
    auto joints = readIndices(item, "joints", context);
    if (!name)
        return std::unexpected(name.error());
    if (!inverseBind)
        return std::unexpected(inverseBind.error());
    if (!skeleton)
        return std::unexpected(skeleton.error());
    if (!joints)
        return std::unexpected(joints.error());
    if (joints->empty())
        return std::unexpected(context + ".joints must not be empty");
    return Skin{std::move(*name), *inverseBind, *skeleton, std::move(*joints)};
}

Result<Animation> parseAnimation(const Value &item, const std::string &context,
                                 std::vector<std::string> &warnings)
{
    Animation animation;
    auto name = readString(item, "name", context);
    if (!name)
        return std::unexpected(name.error());
    animation.name = std::move(*name);

    auto samplers = optionalArray(item, "samplers", context);
    auto channels = optionalArray(item, "channels", context);
    if (!samplers)
        return std::unexpected(samplers.error());
    if (!channels)
        return std::unexpected(channels.error());
    if (!*samplers || !*channels)
        return std::unexpected(context + " needs samplers and channels");
    for (std::size_t i = 0; i < (*samplers)->size(); ++i)
    {
        const Value &value = (**samplers)[i];
        const std::string samplerContext = context + "." + where("samplers", i);
        if (!value.AsObject())
            return std::unexpected(samplerContext + " must be an object");
        auto input = readIndex(value, "input", samplerContext, true);
        auto output = readIndex(value, "output", samplerContext, true);
        auto interpolation = readString(value, "interpolation", samplerContext);
        if (!input)
            return std::unexpected(input.error());
        if (!output)
            return std::unexpected(output.error());
        if (!interpolation)
            return std::unexpected(interpolation.error());
        AnimationSampler sampler{*input, *output, Interpolation::Linear};
        if (*interpolation == "STEP")
            sampler.interpolation = Interpolation::Step;
        else if (*interpolation == "CUBICSPLINE")
            sampler.interpolation = Interpolation::CubicSpline;
        else if (!interpolation->empty() && *interpolation != "LINEAR")
            return std::unexpected(samplerContext + ".interpolation '" + *interpolation + "' is not valid");
        animation.samplers.push_back(sampler);
    }
    for (std::size_t i = 0; i < (*channels)->size(); ++i)
    {
        const Value &value = (**channels)[i];
        const std::string channelContext = context + "." + where("channels", i);
        if (!value.AsObject())
            return std::unexpected(channelContext + " must be an object");
        auto sampler = readIndex(value, "sampler", channelContext, true);
        auto target = optionalObject(value, "target", channelContext);
        if (!sampler)
            return std::unexpected(sampler.error());
        if (!target)
            return std::unexpected(target.error());
        if (!*target)
            return std::unexpected(channelContext + ".target is required");
        if (static_cast<std::size_t>(*sampler) >= animation.samplers.size())
            return std::unexpected(channelContext + " refers to missing sampler " + std::to_string(*sampler));
        const std::string targetContext = channelContext + ".target";
        auto node = readIndex(**target, "node", targetContext);
        auto path = readString(**target, "path", targetContext);
        if (!node)
            return std::unexpected(node.error());
        if (!path)
            return std::unexpected(path.error());
        AnimationChannel channel{*sampler, *node, AnimationPath::Translation};
        if (*path == "translation")
            channel.path = AnimationPath::Translation;
        else if (*path == "rotation")
            channel.path = AnimationPath::Rotation;
        else if (*path == "scale")
            channel.path = AnimationPath::Scale;
        else if (*path == "weights")
            channel.path = AnimationPath::Weights;
        else
        {
            // KHR_animation_pointer 같은 확장 경로는 아직 다루지 않는다.
            warnings.push_back(channelContext + ": unsupported path '" + *path + "' skipped");
            continue;
        }
        animation.channels.push_back(channel);
    }
    return animation;
}

// --- 문서 전체 검증 ---

Result<void> validateDocument(Document &document)
{
    for (std::size_t i = 0; i < document.bufferViews.size(); ++i)
    {
        const auto &view = document.bufferViews[i];
        const std::string context = where("bufferViews", i);
        if (auto valid = validateIndex(view.buffer, document.buffers.size(), context, "buffer"); !valid)
            return valid;
        if (!fitsIn(view.byteOffset, view.byteLength,
                    document.buffers[static_cast<std::size_t>(view.buffer)].size()))
            return std::unexpected(context + " is outside its buffer");
    }
    const auto viewFits = [&](int viewIndex, std::uint64_t offset, std::uint64_t bytes)
    { return fitsIn(offset, bytes, document.bufferViews[static_cast<std::size_t>(viewIndex)].byteLength); };
    for (std::size_t i = 0; i < document.accessors.size(); ++i)
    {
        const auto &accessor = document.accessors[i];
        const std::string context = where("accessors", i);
        const std::uint64_t elementSize =
            componentSize(accessor.componentType) * static_cast<std::uint64_t>(accessor.componentCount);
        if (auto valid =
                validateIndex(accessor.bufferView, document.bufferViews.size(), context, "bufferView");
            !valid)
            return valid;
        if (accessor.bufferView != none)
        {
            const auto &view = document.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
            const std::uint64_t stride = view.byteStride != 0 ? view.byteStride : elementSize;
            if (stride < elementSize)
                return std::unexpected(context + ": byteStride is smaller than one element");
            const auto bytes = stridedSpan(accessor.count, stride, elementSize);
            if (!bytes || !viewFits(accessor.bufferView, accessor.byteOffset, *bytes))
                return std::unexpected(context + " is outside its bufferView");
        }
        else if (accessor.count > maxZeroAccessorBytes / elementSize)
            return std::unexpected(context + " without bufferView is too large");
        if (accessor.sparse)
        {
            const auto &sparse = *accessor.sparse;
            if (auto valid = validateIndex(sparse.indicesBufferView, document.bufferViews.size(), context,
                                           "bufferView");
                !valid)
                return valid;
            if (auto valid = validateIndex(sparse.valuesBufferView, document.bufferViews.size(), context,
                                           "bufferView");
                !valid)
                return valid;
            const auto indexBytes = stridedSpan(sparse.count, componentSize(sparse.indicesComponentType),
                                                componentSize(sparse.indicesComponentType));
            const auto valueBytes = stridedSpan(sparse.count, elementSize, elementSize);
            if (!indexBytes || !viewFits(sparse.indicesBufferView, sparse.indicesByteOffset, *indexBytes) ||
                !valueBytes || !viewFits(sparse.valuesBufferView, sparse.valuesByteOffset, *valueBytes))
                return std::unexpected(context + ".sparse is outside its bufferView");
        }
    }
    for (std::size_t m = 0; m < document.meshes.size(); ++m)
        for (std::size_t p = 0; p < document.meshes[m].primitives.size(); ++p)
        {
            const auto &primitive = document.meshes[m].primitives[p];
            const std::string context = where("meshes", m) + "." + where("primitives", p);
            for (const auto &attribute : primitive.attributes)
                if (auto valid =
                        validateIndex(attribute.accessor, document.accessors.size(), context, "accessor");
                    !valid)
                    return valid;
            if (auto valid = validateIndex(primitive.indices, document.accessors.size(), context, "accessor");
                !valid)
                return valid;
            if (auto valid =
                    validateIndex(primitive.material, document.materials.size(), context, "material");
                !valid)
                return valid;
        }
    for (std::size_t i = 0; i < document.materials.size(); ++i)
        for (const auto &reference :
             {document.materials[i].baseColorTexture, document.materials[i].emissiveTexture,
              document.materials[i].normalTexture})
            if (auto valid = validateIndex(reference.texture, document.textures.size(), where("materials", i),
                                           "texture");
                !valid)
                return valid;
    for (std::size_t i = 0; i < document.textures.size(); ++i)
    {
        if (auto valid = validateIndex(document.textures[i].source, document.images.size(),
                                       where("textures", i), "image");
            !valid)
            return valid;
        if (auto valid = validateIndex(document.textures[i].sampler, document.samplers.size(),
                                       where("textures", i), "sampler");
            !valid)
            return valid;
    }
    for (std::size_t i = 0; i < document.images.size(); ++i)
        if (auto valid = validateIndex(document.images[i].bufferView, document.bufferViews.size(),
                                       where("images", i), "bufferView");
            !valid)
            return valid;

    // 노드: 범위 → 부모 하나 → 루트에서 모두 도달(순환 없음).
    auto &nodes = document.nodes;
    for (std::size_t i = 0; i < nodes.size(); ++i)
    {
        const std::string context = where("nodes", i);
        if (auto valid = validateIndex(nodes[i].mesh, document.meshes.size(), context, "mesh"); !valid)
            return valid;
        if (auto valid = validateIndex(nodes[i].skin, document.skins.size(), context, "skin"); !valid)
            return valid;
        for (const int child : nodes[i].children)
        {
            if (auto valid = validateIndex(child, nodes.size(), context, "node"); !valid)
                return valid;
            auto &childNode = nodes[static_cast<std::size_t>(child)];
            if (childNode.parent != none || static_cast<std::size_t>(child) == i)
                return std::unexpected(context + ": node " + std::to_string(child) +
                                       " has more than one parent");
            childNode.parent = static_cast<int>(i);
        }
    }
    std::vector<int> stack;
    std::size_t reached = 0;
    for (std::size_t i = 0; i < nodes.size(); ++i)
        if (nodes[i].parent == none)
            stack.push_back(static_cast<int>(i));
    while (!stack.empty())
    {
        const int index = stack.back();
        stack.pop_back();
        ++reached;
        for (const int child : nodes[static_cast<std::size_t>(index)].children)
            stack.push_back(child);
    }
    if (reached != nodes.size())
        return std::unexpected(std::string("glTF node hierarchy contains a cycle"));

    for (std::size_t i = 0; i < document.scenes.size(); ++i)
        for (const int root : document.scenes[i].nodes)
        {
            const std::string context = where("scenes", i);
            if (auto valid = validateIndex(root, nodes.size(), context, "node"); !valid)
                return valid;
            if (nodes[static_cast<std::size_t>(root)].parent != none)
                return std::unexpected(context + ": node " + std::to_string(root) + " is not a root node");
        }
    if (auto valid = validateIndex(document.scene, document.scenes.size(), "glTF.scene", "scene"); !valid)
        return valid;

    for (std::size_t i = 0; i < document.skins.size(); ++i)
    {
        const auto &skin = document.skins[i];
        const std::string context = where("skins", i);
        if (auto valid = validateIndex(skin.skeleton, nodes.size(), context, "node"); !valid)
            return valid;
        for (const int joint : skin.joints)
            if (auto valid = validateIndex(joint, nodes.size(), context, "node"); !valid)
                return valid;
        if (auto valid =
                validateIndex(skin.inverseBindMatrices, document.accessors.size(), context, "accessor");
            !valid)
            return valid;
        if (skin.inverseBindMatrices != none)
        {
            const auto &accessor = document.accessors[static_cast<std::size_t>(skin.inverseBindMatrices)];
            if (accessor.componentCount != 16 || accessor.componentType != ComponentType::Float ||
                accessor.count < skin.joints.size())
                return std::unexpected(context + ".inverseBindMatrices must be MAT4 float, one per joint");
        }
    }
    for (std::size_t i = 0; i < document.animations.size(); ++i)
    {
        const auto &animation = document.animations[i];
        const std::string context = where("animations", i);
        for (const auto &sampler : animation.samplers)
        {
            if (auto valid = validateIndex(sampler.input, document.accessors.size(), context, "accessor");
                !valid)
                return valid;
            if (auto valid = validateIndex(sampler.output, document.accessors.size(), context, "accessor");
                !valid)
                return valid;
            const auto &input = document.accessors[static_cast<std::size_t>(sampler.input)];
            if (input.componentCount != 1 || input.componentType != ComponentType::Float)
                return std::unexpected(context + ": sampler input must be SCALAR float");
        }
        for (const auto &channel : animation.channels)
            if (auto valid = validateIndex(channel.node, nodes.size(), context, "node"); !valid)
                return valid;
    }
    return {};
}

struct Container
{
    std::string_view json;
    std::optional<std::span<const std::byte>> bin;
};

// GLB: 12바이트 헤더(매직, 버전 2, 전체 길이) + JSON 청크 + 선택 BIN 청크.
Result<Container> splitGlb(std::span<const std::byte> bytes)
{
    if (bytes.size() < 20)
        return std::unexpected(std::string("GLB is too short"));
    const auto version = loadLittle<std::uint32_t>(bytes.data() + 4);
    const auto length = loadLittle<std::uint32_t>(bytes.data() + 8);
    if (version != 2)
        return std::unexpected("GLB version " + std::to_string(version) + " is not supported");
    if (length < 20 || length > bytes.size())
        return std::unexpected(std::string("GLB length does not match the file size"));
    bytes = bytes.first(length);

    Container container;
    std::size_t offset = 12;
    bool first = true;
    while (offset < bytes.size())
    {
        if (bytes.size() - offset < 8)
            return std::unexpected(std::string("GLB chunk header is truncated"));
        const auto chunkLength = loadLittle<std::uint32_t>(bytes.data() + offset);
        const auto chunkType = loadLittle<std::uint32_t>(bytes.data() + offset + 4);
        offset += 8;
        if (chunkLength > bytes.size() - offset)
            return std::unexpected(std::string("GLB chunk is outside the file"));
        const auto data = bytes.subspan(offset, chunkLength);
        offset += chunkLength;
        if (first)
        {
            if (chunkType != glbChunkJson)
                return std::unexpected(std::string("GLB must start with a JSON chunk"));
            container.json = {reinterpret_cast<const char *>(data.data()), data.size()};
            first = false;
        }
        else if (chunkType == glbChunkBin && !container.bin)
            container.bin = data;
        // 모르는 청크는 스펙대로 건너뛴다.
    }
    return container;
}

Result<void> checkAssetAndExtensions(const Value &root)
{
    auto asset = optionalObject(root, "asset", "glTF");
    if (!asset)
        return std::unexpected(asset.error());
    if (!*asset)
        return std::unexpected(std::string("glTF.asset is required"));
    auto version = readString(**asset, "version", "glTF.asset");
    auto minVersion = readString(**asset, "minVersion", "glTF.asset");
    if (!version)
        return std::unexpected(version.error());
    if (!minVersion)
        return std::unexpected(minVersion.error());
    const std::string &required = minVersion->empty() ? *version : *minVersion;
    if (!required.starts_with("2."))
        return std::unexpected("glTF version '" + required + "' is not supported (2.x only)");

    auto extensions = optionalArray(root, "extensionsRequired", "glTF");
    if (!extensions)
        return std::unexpected(extensions.error());
    if (*extensions)
        for (const auto &value : **extensions)
        {
            const auto *name = value.AsString();
            if (!name)
                return std::unexpected(std::string("glTF.extensionsRequired must contain strings"));
            if (std::ranges::find(supportedRequiredExtensions, std::string_view{*name}) ==
                supportedRequiredExtensions.end())
                return std::unexpected("Unsupported required glTF extension: " + *name);
        }
    return {};
}

std::expected<Document, AssetLoadError> parseDocument(std::span<const std::byte> bytes,
                                                      const std::filesystem::path &baseDirectory,
                                                      const LoadOptions &options)
{
    const auto fail = [](std::string message) { return std::unexpected(AssetLoadError{std::move(message)}); };

    Container container;
    if (bytes.size() >= 4 && loadLittle<std::uint32_t>(bytes.data()) == glbMagic)
    {
        auto split = splitGlb(bytes);
        if (!split)
            return fail(split.error());
        container = *split;
    }
    else
        container.json = {reinterpret_cast<const char *>(bytes.data()), bytes.size()};

    auto parsed = json::Parse(container.json);
    if (!parsed)
        return std::unexpected(
            AssetLoadError{"glTF JSON: " + parsed.error().message, {}, parsed.error().line});
    const Value &root = *parsed;
    if (!root.AsObject())
        return fail("glTF root must be an object");
    if (auto checked = checkAssetAndExtensions(root); !checked)
        return fail(checked.error());

    Document document;
    document.baseDirectory = baseDirectory;

    // 버퍼: 실제 바이트를 먼저 모은다. 이후 검증은 모두 이 크기를 기준으로 한다.
    auto buffers = optionalArray(root, "buffers", "glTF");
    if (!buffers)
        return fail(buffers.error());
    if (*buffers)
        for (std::size_t i = 0; i < (*buffers)->size(); ++i)
        {
            const Value &item = (**buffers)[i];
            const std::string context = where("buffers", i);
            if (!item.AsObject())
                return fail(context + " must be an object");
            auto length = readSize(item, "byteLength", context, std::nullopt);
            auto uri = readString(item, "uri", context);
            if (!length)
                return fail(length.error());
            if (!uri)
                return fail(uri.error());
            std::vector<std::byte> data;
            if (uri->empty())
            {
                // uri 없는 첫 버퍼는 GLB BIN 청크다(청크는 4바이트 정렬 패딩을 가질 수 있다).
                if (i != 0 || !container.bin)
                    return fail(context + " has no uri and no GLB BIN chunk");
                data.assign(container.bin->begin(), container.bin->end());
            }
            else
            {
                auto read = readUri(*uri, baseDirectory, options.maxFileBytes, context);
                if (!read)
                    return std::unexpected(std::move(read.error()));
                data = std::move(*read);
            }
            if (data.size() < *length)
                return fail(context + " has fewer bytes than byteLength");
            data.resize(*length);
            document.buffers.push_back(std::move(data));
        }

    const auto load = [&](auto &target, std::string_view key, auto parse) -> Result<void>
    {
        auto items =
            readObjects<typename std::remove_reference_t<decltype(target)>::value_type>(root, key, parse);
        if (!items)
            return std::unexpected(items.error());
        target = std::move(*items);
        return {};
    };
    Result<void> loaded = load(document.bufferViews, "bufferViews", parseBufferView);
    if (loaded)
        loaded = load(document.accessors, "accessors", parseAccessor);
    if (loaded)
        loaded = load(document.meshes, "meshes", parseMesh);
    if (loaded)
        loaded = load(document.materials, "materials", parseMaterial);
    if (loaded)
        loaded = load(document.textures, "textures", parseTexture);
    if (loaded)
        loaded = load(document.samplers, "samplers", parseSampler);
    if (loaded)
        loaded = load(document.images, "images", parseImage);
    if (loaded)
        loaded = load(document.nodes, "nodes", parseNode);
    if (loaded)
        loaded = load(document.scenes, "scenes", parseScene);
    if (loaded)
        loaded = load(document.skins, "skins", parseSkin);
    if (loaded)
        loaded = load(document.animations, "animations", [&](const Value &item, const std::string &context)
                      { return parseAnimation(item, context, document.warnings); });
    if (!loaded)
        return fail(loaded.error());

    auto scene = readIndex(root, "scene", "glTF");
    if (!scene)
        return fail(scene.error());
    document.scene = *scene;

    if (auto valid = validateDocument(document); !valid)
        return fail(valid.error());

    for (std::size_t m = 0; m < document.meshes.size(); ++m)
        for (const auto &primitive : document.meshes[m].primitives)
            if (primitive.hasMorphTargets)
            {
                document.warnings.push_back(where("meshes", m) + ": morph targets are ignored");
                break;
            }
    return document;
}

// accessor 요소를 하나씩 읽어 convert(바이트, 성분 타입)으로 바꾼다. sparse 치환까지 적용한다.
template <class T, class Convert>
Result<std::vector<T>> gather(const Document &document, int accessorIndex, Convert convert)
{
    if (accessorIndex < 0 || static_cast<std::size_t>(accessorIndex) >= document.accessors.size())
        return std::unexpected("accessor " + std::to_string(accessorIndex) + " does not exist");
    const Accessor &accessor = document.accessors[static_cast<std::size_t>(accessorIndex)];
    const std::size_t components = static_cast<std::size_t>(accessor.componentCount);
    const std::size_t size = componentSize(accessor.componentType);
    const std::size_t elementSize = size * components;
    std::vector<T> result(accessor.count * components, T{});

    const auto viewBytes = [&](int viewIndex, std::size_t offset)
    {
        const BufferView &view = document.bufferViews[static_cast<std::size_t>(viewIndex)];
        return document.buffers[static_cast<std::size_t>(view.buffer)].data() + view.byteOffset + offset;
    };
    if (accessor.bufferView != none)
    {
        const BufferView &view = document.bufferViews[static_cast<std::size_t>(accessor.bufferView)];
        const std::size_t stride = view.byteStride != 0 ? view.byteStride : elementSize;
        const std::byte *base = viewBytes(accessor.bufferView, accessor.byteOffset);
        for (std::size_t element = 0; element < accessor.count; ++element)
            for (std::size_t c = 0; c < components; ++c)
            {
                auto value = convert(base + element * stride + c * size, accessor.componentType);
                if (!value)
                    return std::unexpected(value.error());
                result[element * components + c] = *value;
            }
    }
    if (accessor.sparse)
    {
        const AccessorSparse &sparse = *accessor.sparse;
        const std::size_t indexSize = componentSize(sparse.indicesComponentType);
        const std::byte *indices = viewBytes(sparse.indicesBufferView, sparse.indicesByteOffset);
        const std::byte *values = viewBytes(sparse.valuesBufferView, sparse.valuesByteOffset);
        for (std::size_t k = 0; k < sparse.count; ++k)
        {
            const auto target = loadUnsigned(indices + k * indexSize, sparse.indicesComponentType);
            if (!target || *target >= accessor.count)
                return std::unexpected("accessor " + std::to_string(accessorIndex) +
                                       ": sparse index out of range");
            for (std::size_t c = 0; c < components; ++c)
            {
                auto value = convert(values + k * elementSize + c * size, accessor.componentType);
                if (!value)
                    return std::unexpected(value.error());
                result[*target * components + c] = *value;
            }
        }
    }
    return result;
}
} // namespace

int Primitive::FindAttribute(std::string_view name) const noexcept
{
    for (const auto &attribute : attributes)
        if (attribute.name == name)
            return attribute.accessor;
    return none;
}

std::expected<Document, AssetLoadError> LoadFile(const std::filesystem::path &path,
                                                 const LoadOptions &options)
{
    auto bytes = io::ReadWholeFile(path, options.maxFileBytes);
    if (!bytes)
        return std::unexpected(std::move(bytes.error()));
    auto document = LoadFromMemory(std::as_bytes(std::span{*bytes}), path.parent_path(), options);
    if (!document && document.error().path.empty())
        document.error().path = path;
    return document;
}

std::expected<Document, AssetLoadError> LoadFromMemory(std::span<const std::byte> bytes,
                                                       const std::filesystem::path &baseDirectory,
                                                       const LoadOptions &options)
{
    return parseDocument(bytes, baseDirectory, options);
}

std::expected<std::vector<float>, std::string> ReadAccessorFloats(const Document &document, int accessor)
{
    const bool normalized = accessor >= 0 && static_cast<std::size_t>(accessor) < document.accessors.size() &&
                            document.accessors[static_cast<std::size_t>(accessor)].normalized;
    return gather<float>(document, accessor,
                         [normalized](const std::byte *bytes, ComponentType type) -> Result<float>
                         { return loadComponentAsFloat(bytes, type, normalized); });
}

std::expected<std::vector<std::uint32_t>, std::string> ReadAccessorIndices(const Document &document,
                                                                           int accessor)
{
    if (accessor >= 0 && static_cast<std::size_t>(accessor) < document.accessors.size())
    {
        const auto &info = document.accessors[static_cast<std::size_t>(accessor)];
        if (info.componentCount != 1 || info.normalized)
            return std::unexpected("accessor " + std::to_string(accessor) +
                                   " is not a SCALAR index accessor");
    }
    return gather<std::uint32_t>(
        document, accessor,
        [accessor](const std::byte *bytes, ComponentType type) -> Result<std::uint32_t>
        {
            if (const auto value = loadUnsigned(bytes, type))
                return *value;
            return std::unexpected("accessor " + std::to_string(accessor) +
                                   ": indices must be unsigned integers");
        });
}

std::expected<std::vector<std::byte>, AssetLoadError> ReadImageBytes(const Document &document, int imageIndex,
                                                                     const LoadOptions &options)
{
    if (imageIndex < 0 || static_cast<std::size_t>(imageIndex) >= document.images.size())
        return std::unexpected(AssetLoadError{"image " + std::to_string(imageIndex) + " does not exist"});
    const Image &image = document.images[static_cast<std::size_t>(imageIndex)];
    if (image.bufferView != none)
    {
        const BufferView &view = document.bufferViews[static_cast<std::size_t>(image.bufferView)];
        const auto &buffer = document.buffers[static_cast<std::size_t>(view.buffer)];
        const auto first = buffer.begin() + static_cast<std::ptrdiff_t>(view.byteOffset);
        return std::vector<std::byte>(first, first + static_cast<std::ptrdiff_t>(view.byteLength));
    }
    return readUri(image.uri, document.baseDirectory, options.maxFileBytes,
                   where("images", static_cast<std::size_t>(imageIndex)));
}

std::optional<std::vector<std::byte>> DecodeBase64(std::string_view text)
{
    const auto sextet = [](char c) -> int
    {
        if (c >= 'A' && c <= 'Z')
            return c - 'A';
        if (c >= 'a' && c <= 'z')
            return c - 'a' + 26;
        if (c >= '0' && c <= '9')
            return c - '0' + 52;
        if (c == '+')
            return 62;
        if (c == '/')
            return 63;
        return -1;
    };
    while (text.ends_with('='))
        text.remove_suffix(1);
    if (text.size() % 4 == 1)
        return std::nullopt;
    std::vector<std::byte> result;
    result.reserve(text.size() / 4 * 3 + 2);
    std::uint32_t accumulator = 0;
    int bits = 0;
    for (const char c : text)
    {
        const int value = sextet(c);
        if (value < 0)
            return std::nullopt;
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8)
        {
            bits -= 8;
            result.push_back(static_cast<std::byte>((accumulator >> bits) & 0xFF));
        }
    }
    return result;
}
} // namespace sr::gltf

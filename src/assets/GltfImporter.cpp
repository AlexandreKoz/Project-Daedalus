#include "assets/GltfImporter.h"

#include "assets/ImageDecoder.h"

#include "core/Json.h"
#include "core/Sha256.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>
#include <unordered_set>
#include <utility>

namespace daedalus
{
namespace
{
constexpr std::uint32_t kGlbMagic = 0x46546C67U;
constexpr std::uint32_t kJsonChunk = 0x4E4F534AU;
constexpr std::uint32_t kBinChunk = 0x004E4942U;

struct BufferData
{
    std::vector<std::byte> bytes;
    std::string identity;
};

struct BufferViewData
{
    std::size_t buffer = 0;
    std::uint64_t offset = 0;
    std::uint64_t length = 0;
    std::uint64_t stride = 0;
};

struct AccessorData
{
    std::optional<std::size_t> buffer_view;
    std::uint64_t offset = 0;
    std::uint64_t count = 0;
    std::uint32_t component_type = 0;
    std::string type;
    std::vector<float> declared_min;
    std::vector<float> declared_max;
    bool normalized = false;
    bool sparse = false;
};

struct ParsedDocument
{
    JsonValue root;
    std::vector<std::byte> source_bytes;
    std::vector<std::byte> glb_binary;
    std::string format;
};

class ImportFailure final : public std::runtime_error
{
public:
    ImportFailure(ImportStatus status, Diagnostic diagnostic)
        : std::runtime_error(diagnostic.message), status_(status), diagnostic_(std::move(diagnostic))
    {
    }
    [[nodiscard]] ImportStatus status() const noexcept { return status_; }
    [[nodiscard]] const Diagnostic& diagnostic() const noexcept { return diagnostic_; }

private:
    ImportStatus status_;
    Diagnostic diagnostic_;
};

[[noreturn]] void fail(ImportStatus status,
                       DiagnosticCode code,
                       std::string location,
                       std::string message,
                       std::string expected = {},
                       std::string observed = {})
{
    throw ImportFailure(status, {DiagnosticSeverity::error, code, DiagnosticDisposition::rejected,
                                 std::move(location), std::move(message), std::move(expected), std::move(observed)});
}

void add_warning(std::vector<Diagnostic>& diagnostics,
                 DiagnosticCode code,
                 DiagnosticDisposition disposition,
                 std::string location,
                 std::string message,
                 std::string expected = {},
                 std::string observed = {})
{
    diagnostics.push_back({DiagnosticSeverity::warning, code, disposition, std::move(location),
                           std::move(message), std::move(expected), std::move(observed)});
}

void add_information(std::vector<Diagnostic>& diagnostics,
                     DiagnosticCode code,
                     DiagnosticDisposition disposition,
                     std::string location,
                     std::string message)
{
    diagnostics.push_back({DiagnosticSeverity::information, code, disposition, std::move(location), std::move(message), {}, {}});
}

[[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path,
                                               std::uint64_t maximum_bytes,
                                               std::string_view location,
                                               ImportStatus missing_status = ImportStatus::io_failure,
                                               DiagnosticCode missing_code = DiagnosticCode::io_open_failed)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error)
    {
        fail(missing_status, missing_code, std::string(location),
             "unable to determine file size", {}, error.message());
    }
    if (size > maximum_bytes || size > std::numeric_limits<std::size_t>::max())
    {
        fail(ImportStatus::resource_limit, DiagnosticCode::source_too_large, std::string(location),
             "file exceeds configured resource limit", std::to_string(maximum_bytes), std::to_string(size));
    }
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        fail(missing_status, missing_code, std::string(location), "unable to open file");
    }
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty()) input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty())
    {
        fail(missing_status, missing_code, std::string(location), "unable to read complete file");
    }
    return bytes;
}

[[nodiscard]] std::uint32_t read_u32_le(std::span<const std::byte> bytes, std::size_t offset)
{
    if (offset + 4 > bytes.size())
        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_glb_chunk, "glb", "truncated 32-bit field");
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3]) << 24U);
}

[[nodiscard]] ParsedDocument parse_document(std::vector<std::byte> source_bytes)
{
    ParsedDocument document;
    document.source_bytes = std::move(source_bytes);
    const auto bytes = std::span<const std::byte>(document.source_bytes);
    std::string json_text;
    if (bytes.size() >= 4 && read_u32_le(bytes, 0) == kGlbMagic)
    {
        document.format = "glb";
        if (bytes.size() < 12)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_glb_header, "glb.header", "GLB header is truncated");
        const std::uint32_t version = read_u32_le(bytes, 4);
        const std::uint32_t declared_length = read_u32_le(bytes, 8);
        if (version != 2)
            fail(ImportStatus::unsupported_feature, DiagnosticCode::unsupported_version, "glb.header.version",
                 "unsupported GLB version", "2", std::to_string(version));
        if (declared_length != bytes.size())
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_glb_header, "glb.header.length",
                 "GLB declared length does not match file size", std::to_string(bytes.size()), std::to_string(declared_length));
        std::size_t offset = 12;
        bool json_found = false;
        while (offset < bytes.size())
        {
            if (offset + 8 > bytes.size())
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_glb_chunk, "glb.chunk", "GLB chunk header is truncated");
            const std::uint32_t chunk_length = read_u32_le(bytes, offset);
            const std::uint32_t chunk_type = read_u32_le(bytes, offset + 4);
            offset += 8;
            if (chunk_length > bytes.size() - offset)
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_glb_chunk, "glb.chunk",
                     "GLB chunk length exceeds remaining file bytes");
            const auto chunk = bytes.subspan(offset, chunk_length);
            if (chunk_type == kJsonChunk)
            {
                if (json_found)
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_glb_chunk, "glb.chunk.json", "GLB contains more than one JSON chunk");
                json_found = true;
                json_text.assign(reinterpret_cast<const char*>(chunk.data()), chunk.size());
                while (!json_text.empty() && (json_text.back() == '\0' || json_text.back() == ' ' || json_text.back() == '\t' || json_text.back() == '\r' || json_text.back() == '\n'))
                    json_text.pop_back();
            }
            else if (chunk_type == kBinChunk)
            {
                if (!document.glb_binary.empty())
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_glb_chunk, "glb.chunk.bin", "GLB contains more than one BIN chunk");
                document.glb_binary.assign(chunk.begin(), chunk.end());
            }
            offset += chunk_length;
        }
        if (!json_found)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_glb_chunk, "glb.chunk.json", "GLB does not contain a JSON chunk");
    }
    else
    {
        document.format = "gltf";
        json_text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }

    try
    {
        document.root = parse_json(json_text);
    }
    catch (const JsonError& error)
    {
        fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json,
             "json.byte[" + std::to_string(error.offset()) + "]", error.what());
    }
    if (!document.root.is_object())
        fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, "json", "glTF root must be a JSON object");
    return document;
}

[[nodiscard]] const JsonValue::Object& require_object(const JsonValue& value, std::string_view location)
{
    if (!value.is_object())
        fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "expected JSON object");
    return value.as_object();
}

[[nodiscard]] const JsonValue::Array& require_array(const JsonValue& value, std::string_view location)
{
    if (!value.is_array())
        fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "expected JSON array");
    return value.as_array();
}

[[nodiscard]] std::string require_string(const JsonValue& value, std::string_view location)
{
    if (!value.is_string())
        fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "expected JSON string");
    return value.as_string();
}

[[nodiscard]] double require_number(const JsonValue& value, std::string_view location)
{
    if (!value.is_number())
        fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "expected JSON number");
    return value.as_number();
}

[[nodiscard]] std::uint64_t require_uint(const JsonValue& value, std::string_view location)
{
    const double number = require_number(value, location);
    if (number < 0.0 || number > static_cast<double>(std::numeric_limits<std::uint64_t>::max()) || std::floor(number) != number)
        fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "expected non-negative integer");
    return static_cast<std::uint64_t>(number);
}

[[nodiscard]] std::optional<std::uint64_t> optional_uint(const JsonValue::Object& object, std::string_view key, std::string_view location)
{
    const auto iterator = object.find(key);
    if (iterator == object.end()) return std::nullopt;
    return require_uint(iterator->second, std::string(location) + "." + std::string(key));
}

[[nodiscard]] std::string optional_string(const JsonValue::Object& object, std::string_view key, std::string_view location)
{
    const auto iterator = object.find(key);
    return iterator == object.end() ? std::string{} : require_string(iterator->second, std::string(location) + "." + std::string(key));
}

[[nodiscard]] bool optional_bool(const JsonValue::Object& object, std::string_view key, bool fallback, std::string_view location)
{
    const auto iterator = object.find(key);
    if (iterator == object.end()) return fallback;
    if (!iterator->second.is_bool())
        fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location) + "." + std::string(key), "expected JSON boolean");
    return iterator->second.as_bool();
}

[[nodiscard]] const JsonValue::Array* optional_array(const JsonValue::Object& object, std::string_view key, std::string_view location)
{
    const auto iterator = object.find(key);
    if (iterator == object.end()) return nullptr;
    return &require_array(iterator->second, std::string(location) + "." + std::string(key));
}

[[nodiscard]] const JsonValue::Object* optional_object(const JsonValue::Object& object, std::string_view key, std::string_view location)
{
    const auto iterator = object.find(key);
    if (iterator == object.end()) return nullptr;
    return &require_object(iterator->second, std::string(location) + "." + std::string(key));
}

[[nodiscard]] std::uint64_t checked_add(std::uint64_t left, std::uint64_t right, std::string_view location)
{
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_buffer_range, std::string(location), "integer overflow while calculating byte range");
    return left + right;
}

[[nodiscard]] std::uint64_t checked_multiply(std::uint64_t left, std::uint64_t right, std::string_view location)
{
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_buffer_range, std::string(location), "integer overflow while calculating byte range");
    return left * right;
}

class ResourceBudget final
{
public:
    ResourceBudget(const ImportSettings& settings, ResourceUsage& usage)
        : retained_limit_(settings.maximum_total_decoded_bytes), peak_limit_(settings.maximum_peak_bytes), usage_(usage)
    {
    }

    void add_source(std::uint64_t bytes, std::string_view location) { add(bytes, usage_.source_payload_bytes, "source_payload", location); }
    void add_buffer(std::uint64_t bytes, std::string_view location) { add(bytes, usage_.buffer_payload_bytes, "buffer_payload", location); }
    void add_encoded_image(std::uint64_t bytes, std::string_view location) { add(bytes, usage_.encoded_image_bytes, "encoded_image", location); }
    void add_geometry(std::uint64_t bytes, std::string_view location) { add(bytes, usage_.canonical_geometry_bytes, "canonical_geometry", location); }
    void add_decoded_image(std::uint64_t bytes, std::string_view location) { add(bytes, usage_.decoded_image_bytes, "decoded_image", location); }

    void preflight_retain(std::uint64_t bytes, std::string_view category, std::string_view location) const
    {
        const std::uint64_t candidate = checked_add(usage_.retained_bytes, bytes, location);
        if (candidate > retained_limit_)
            fail(ImportStatus::resource_limit, DiagnosticCode::resource_budget_exceeded,
                 std::string(location), "cumulative retained resource budget would be exceeded before allocation",
                 std::string(category) + " cumulative <= " + std::to_string(retained_limit_),
                 std::to_string(candidate));
        if (candidate > peak_limit_)
            fail(ImportStatus::resource_limit, DiagnosticCode::resource_budget_exceeded,
                 std::string(location), "conservative peak resource budget would be exceeded before allocation",
                 std::string(category) + " peak <= " + std::to_string(peak_limit_),
                 std::to_string(candidate));
    }

    void observe_scratch(std::uint64_t bytes, std::string_view category, std::string_view location)
    {
        const std::uint64_t candidate = checked_add(usage_.retained_bytes, bytes, location);
        usage_.conservative_peak_bytes = std::max(usage_.conservative_peak_bytes, candidate);
        if (candidate > peak_limit_)
            fail(ImportStatus::resource_limit, DiagnosticCode::resource_budget_exceeded,
                 std::string(location), "conservative peak resource budget exceeded",
                 std::string(category) + " <= " + std::to_string(peak_limit_), std::to_string(candidate));
    }

private:
    void add(std::uint64_t bytes, std::uint64_t& category_counter, std::string_view category, std::string_view location)
    {
        preflight_retain(bytes, category, location);
        category_counter = checked_add(category_counter, bytes, location);
        usage_.retained_bytes = checked_add(usage_.retained_bytes, bytes, location);
        usage_.conservative_peak_bytes = std::max(usage_.conservative_peak_bytes, usage_.retained_bytes);
        if (usage_.retained_bytes > retained_limit_)
            fail(ImportStatus::resource_limit, DiagnosticCode::resource_budget_exceeded,
                 std::string(location), "cumulative retained resource budget exceeded",
                 std::string(category) + " cumulative <= " + std::to_string(retained_limit_),
                 std::to_string(usage_.retained_bytes));
        if (usage_.conservative_peak_bytes > peak_limit_)
            fail(ImportStatus::resource_limit, DiagnosticCode::resource_budget_exceeded,
                 std::string(location), "conservative peak resource budget exceeded",
                 std::string(category) + " peak <= " + std::to_string(peak_limit_),
                 std::to_string(usage_.conservative_peak_bytes));
    }

    std::uint64_t retained_limit_ = 0;
    std::uint64_t peak_limit_ = 0;
    ResourceUsage& usage_;
};

[[nodiscard]] int base64_value(char character) noexcept
{
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
}

[[nodiscard]] std::vector<std::byte> decode_base64(std::string_view value, std::string_view location)
{
    std::string compact;
    compact.reserve(value.size());
    for (const char character : value)
    {
        if (character == '\r' || character == '\n' || character == ' ' || character == '\t') continue;
        compact.push_back(character);
    }
    if (compact.size() % 4U != 0)
        fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location),
             "base64 payload length is not a multiple of four");

    std::vector<std::byte> output;
    output.reserve((compact.size() / 4U) * 3U);
    for (std::size_t offset = 0; offset < compact.size(); offset += 4U)
    {
        const bool final_group = offset + 4U == compact.size();
        const char c0 = compact[offset];
        const char c1 = compact[offset + 1U];
        const char c2 = compact[offset + 2U];
        const char c3 = compact[offset + 3U];
        const int v0 = base64_value(c0);
        const int v1 = base64_value(c1);
        if (v0 < 0 || v1 < 0 || c0 == '=' || c1 == '=')
            fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "invalid base64 quartet");
        if ((c2 == '=' || c3 == '=') && !final_group)
            fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "base64 padding appears before the final quartet");
        if (c2 == '=' && c3 != '=')
            fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "invalid base64 padding");
        const int v2 = c2 == '=' ? 0 : base64_value(c2);
        const int v3 = c3 == '=' ? 0 : base64_value(c3);
        if (v2 < 0 || v3 < 0)
            fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "invalid base64 character");
        if (c2 == '=' && (v1 & 0x0F) != 0)
            fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "nonzero discarded bits in base64 payload");
        if (c3 == '=' && c2 != '=' && (v2 & 0x03) != 0)
            fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "nonzero discarded bits in base64 payload");

        const std::uint32_t packed = (static_cast<std::uint32_t>(v0) << 18U) |
                                     (static_cast<std::uint32_t>(v1) << 12U) |
                                     (static_cast<std::uint32_t>(v2) << 6U) |
                                     static_cast<std::uint32_t>(v3);
        output.push_back(static_cast<std::byte>((packed >> 16U) & 0xFFU));
        if (c2 != '=') output.push_back(static_cast<std::byte>((packed >> 8U) & 0xFFU));
        if (c3 != '=') output.push_back(static_cast<std::byte>(packed & 0xFFU));
    }
    return output;
}

[[nodiscard]] int hex_value(char character) noexcept
{
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

[[nodiscard]] std::string percent_decode(std::string_view uri, std::string_view location)
{
    std::string output;
    output.reserve(uri.size());
    for (std::size_t index = 0; index < uri.size(); ++index)
    {
        if (uri[index] != '%') { output.push_back(uri[index]); continue; }
        if (index + 2 >= uri.size())
            fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "truncated percent escape in URI");
        const int high = hex_value(uri[index + 1]);
        const int low = hex_value(uri[index + 2]);
        if (high < 0 || low < 0)
            fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "invalid percent escape in URI");
        const char decoded = static_cast<char>((high << 4) | low);
        if (decoded == '\0')
            fail(ImportStatus::invalid_source, DiagnosticCode::unsafe_dependency_path, std::string(location), "URI contains an encoded NUL byte");
        output.push_back(decoded);
        index += 2;
    }
    return output;
}

struct DataUri
{
    std::string mime_type;
    std::vector<std::byte> bytes;
};

struct DataUriParts
{
    std::string_view metadata;
    std::string_view payload;
    bool base64 = false;
};

[[nodiscard]] std::optional<DataUriParts> split_data_uri(std::string_view uri, std::string_view location)
{
    if (!uri.starts_with("data:")) return std::nullopt;
    const std::size_t comma = uri.find(',');
    if (comma == std::string_view::npos)
        fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "data URI is missing a comma");
    const std::string_view metadata = uri.substr(5, comma - 5);
    return DataUriParts{metadata, uri.substr(comma + 1), metadata.ends_with(";base64")};
}

[[nodiscard]] std::uint64_t data_uri_decoded_size(std::string_view uri, std::string_view location)
{
    const std::optional<DataUriParts> parts = split_data_uri(uri, location);
    if (!parts.has_value()) return 0;
    if (parts->base64)
    {
        std::uint64_t compact_size = 0;
        char previous = '\0';
        char last = '\0';
        for (const char character : parts->payload)
        {
            if (character == '\r' || character == '\n' || character == ' ' || character == '\t') continue;
            previous = last;
            last = character;
            compact_size = checked_add(compact_size, 1U, location);
        }
        if (compact_size % 4U != 0)
            fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location),
                 "base64 payload length is not a multiple of four");
        const std::uint64_t groups = compact_size / 4U;
        const std::uint64_t padding = last == '=' ? (previous == '=' ? 2U : 1U) : 0U;
        const std::uint64_t maximum = checked_multiply(groups, 3U, location);
        if (padding > maximum)
            fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "invalid base64 padding");
        return maximum - padding;
    }

    std::uint64_t decoded_size = 0;
    for (std::size_t index = 0; index < parts->payload.size(); ++index)
    {
        if (parts->payload[index] == '%')
        {
            if (index + 2U >= parts->payload.size())
                fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location),
                     "truncated percent escape in URI");
            const int high = hex_value(parts->payload[index + 1U]);
            const int low = hex_value(parts->payload[index + 2U]);
            if (high < 0 || low < 0)
                fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location),
                     "invalid percent escape in URI");
            if (((high << 4) | low) == 0)
                fail(ImportStatus::invalid_source, DiagnosticCode::unsafe_dependency_path, std::string(location),
                     "URI contains an encoded NUL byte");
            index += 2U;
        }
        decoded_size = checked_add(decoded_size, 1U, location);
    }
    return decoded_size;
}

[[nodiscard]] std::optional<DataUri> parse_data_uri(std::string_view uri, std::string_view location)
{
    const std::optional<DataUriParts> parts = split_data_uri(uri, location);
    if (!parts.has_value()) return std::nullopt;
    std::string mime(parts->metadata.substr(0, parts->base64 ? parts->metadata.size() - 7U : parts->metadata.size()));
    DataUri result;
    result.mime_type = std::move(mime);
    if (parts->base64) result.bytes = decode_base64(parts->payload, location);
    else
    {
        const std::string decoded = percent_decode(parts->payload, location);
        result.bytes.assign(reinterpret_cast<const std::byte*>(decoded.data()),
                            reinterpret_cast<const std::byte*>(decoded.data() + decoded.size()));
    }
    return result;
}

[[nodiscard]] bool path_has_parent_reference(const std::filesystem::path& path)
{
    for (const auto& component : path)
        if (component == "..") return true;
    return false;
}

[[nodiscard]] bool path_is_within(const std::filesystem::path& root, const std::filesystem::path& candidate)
{
    auto root_iterator = root.begin();
    auto candidate_iterator = candidate.begin();
    for (; root_iterator != root.end(); ++root_iterator, ++candidate_iterator)
    {
        if (candidate_iterator == candidate.end() || *root_iterator != *candidate_iterator) return false;
    }
    return true;
}

[[nodiscard]] std::filesystem::path resolve_dependency(const std::filesystem::path& root,
                                                       std::string_view uri,
                                                       bool reject_path_traversal,
                                                       std::string_view location,
                                                       std::string& normalized_relative)
{
    const std::string decoded = percent_decode(uri, location);
    if (decoded.find('\\') != std::string::npos)
        fail(ImportStatus::invalid_source, DiagnosticCode::unsafe_dependency_path, std::string(location), "backslashes are not accepted in glTF URIs");
    if (decoded.find(':') != std::string::npos)
        fail(ImportStatus::unsupported_feature, DiagnosticCode::unsafe_dependency_path, std::string(location),
             "URI schemes and drive-qualified paths are not supported", "relative local URI", decoded);
    const std::filesystem::path relative = std::filesystem::path(decoded).lexically_normal();
    if (relative.empty() || relative == "." || relative.is_absolute() || relative.has_root_name() ||
        (reject_path_traversal && path_has_parent_reference(relative)))
        fail(ImportStatus::invalid_source, DiagnosticCode::unsafe_dependency_path, std::string(location), "dependency path escapes the asset root", "relative path inside asset root", decoded);
    const std::filesystem::path candidate = root / relative;
    if (reject_path_traversal)
    {
        std::error_code root_error;
        std::error_code candidate_error;
        const std::filesystem::path canonical_root = std::filesystem::weakly_canonical(root, root_error);
        const std::filesystem::path canonical_candidate = std::filesystem::weakly_canonical(candidate, candidate_error);
        if (root_error || candidate_error)
            fail(ImportStatus::io_failure, DiagnosticCode::io_open_failed, std::string(location),
                 "dependency path could not be canonicalized", {}, root_error ? root_error.message() : candidate_error.message());
        if (!path_is_within(canonical_root, canonical_candidate))
            fail(ImportStatus::invalid_source, DiagnosticCode::unsafe_dependency_path, std::string(location),
                 "dependency resolves outside the asset root through a symbolic link", "path inside asset root", canonical_candidate.generic_string());
    }
    normalized_relative = relative.generic_string();
    return candidate;
}

[[nodiscard]] std::vector<std::string> parse_string_array(const JsonValue::Object& root,
                                                          std::string_view key,
                                                          std::string_view location)
{
    std::vector<std::string> result;
    const JsonValue::Array* values = optional_array(root, key, location);
    if (values == nullptr) return result;
    for (std::size_t index = 0; index < values->size(); ++index)
        result.push_back(require_string((*values)[index], std::string(location) + "." + std::string(key) + "[" + std::to_string(index) + "]"));
    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

[[nodiscard]] float number_to_float(const JsonValue& value, std::string_view location)
{
    const double number = require_number(value, location);
    if (number < -static_cast<double>(std::numeric_limits<float>::max()) || number > static_cast<double>(std::numeric_limits<float>::max()))
        fail(ImportStatus::invalid_source, DiagnosticCode::non_finite_data, std::string(location), "number is outside finite float range");
    const float result = static_cast<float>(number);
    if (!std::isfinite(result))
        fail(ImportStatus::invalid_source, DiagnosticCode::non_finite_data, std::string(location), "number is not finite");
    return result;
}

[[nodiscard]] std::vector<float> read_numeric_array(const JsonValue& value,
                                                    std::size_t expected_components,
                                                    std::string_view location)
{
    const auto& array = require_array(value, location);
    if (array.size() != expected_components)
        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_accessor, std::string(location),
             "numeric array has the wrong component count", std::to_string(expected_components), std::to_string(array.size()));
    std::vector<float> values;
    values.reserve(array.size());
    for (std::size_t index = 0; index < array.size(); ++index)
        values.push_back(number_to_float(array[index], std::string(location) + "[" + std::to_string(index) + "]"));
    return values;
}

[[nodiscard]] bool nearly_equal_bounds(float left, float right) noexcept
{
    const float scale = std::max({1.0F, std::abs(left), std::abs(right)});
    return std::abs(left - right) <= 1.0e-5F * scale;
}

[[nodiscard]] Vec3 read_vec3(const JsonValue& value, std::string_view location)
{
    const auto& array = require_array(value, location);
    if (array.size() != 3) fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "expected three numeric components");
    return {number_to_float(array[0], std::string(location) + "[0]"), number_to_float(array[1], std::string(location) + "[1]"), number_to_float(array[2], std::string(location) + "[2]")};
}

[[nodiscard]] Vec4 read_vec4(const JsonValue& value, std::string_view location)
{
    const auto& array = require_array(value, location);
    if (array.size() != 4) fail(ImportStatus::invalid_source, DiagnosticCode::malformed_json, std::string(location), "expected four numeric components");
    return {number_to_float(array[0], std::string(location) + "[0]"), number_to_float(array[1], std::string(location) + "[1]"),
            number_to_float(array[2], std::string(location) + "[2]"), number_to_float(array[3], std::string(location) + "[3]")};
}

[[nodiscard]] std::size_t component_size(std::uint32_t type, std::string_view location)
{
    switch (type)
    {
    case 5120: case 5121: return 1;
    case 5122: case 5123: return 2;
    case 5125: case 5126: return 4;
    default:
        fail(ImportStatus::unsupported_feature, DiagnosticCode::unsupported_component_type, std::string(location),
             "unsupported accessor component type", "5120, 5121, 5122, 5123, 5125, or 5126", std::to_string(type));
    }
}

[[nodiscard]] std::size_t component_count(std::string_view type, std::string_view location)
{
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT2") return 4;
    if (type == "MAT3") return 9;
    if (type == "MAT4") return 16;
    fail(ImportStatus::unsupported_feature, DiagnosticCode::unsupported_accessor_shape, std::string(location), "unsupported accessor shape", {}, std::string(type));
}

[[nodiscard]] double decode_component(const std::byte* bytes, std::uint32_t component_type, bool normalized)
{
    switch (component_type)
    {
    case 5120:
    {
        std::int8_t value{}; std::memcpy(&value, bytes, sizeof(value));
        return normalized ? std::max(static_cast<double>(value) / 127.0, -1.0) : static_cast<double>(value);
    }
    case 5121:
    {
        std::uint8_t value{}; std::memcpy(&value, bytes, sizeof(value));
        return normalized ? static_cast<double>(value) / 255.0 : static_cast<double>(value);
    }
    case 5122:
    {
        std::int16_t value{}; std::memcpy(&value, bytes, sizeof(value));
        return normalized ? std::max(static_cast<double>(value) / 32767.0, -1.0) : static_cast<double>(value);
    }
    case 5123:
    {
        std::uint16_t value{}; std::memcpy(&value, bytes, sizeof(value));
        return normalized ? static_cast<double>(value) / 65535.0 : static_cast<double>(value);
    }
    case 5125:
    {
        std::uint32_t value{}; std::memcpy(&value, bytes, sizeof(value));
        return normalized ? static_cast<double>(value) / 4294967295.0 : static_cast<double>(value);
    }
    case 5126:
    {
        float value{}; std::memcpy(&value, bytes, sizeof(value)); return value;
    }
    default: return 0.0;
    }
}

class AccessorReader final
{
public:
    AccessorReader(const std::vector<BufferData>& buffers,
                   const std::vector<BufferViewData>& views,
                   const std::vector<AccessorData>& accessors)
        : buffers_(buffers), views_(views), accessors_(accessors)
    {
    }

    [[nodiscard]] std::vector<float> read_floats(std::size_t accessor_index, std::size_t expected_components, std::string_view location) const
    {
        const auto [accessor, view, data, stride, element_size] = resolve(accessor_index, location);
        const std::size_t components = component_count(accessor.type, location);
        if (components != expected_components)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_accessor, std::string(location),
                 "accessor has unexpected shape", std::to_string(expected_components) + " components", accessor.type);
        std::vector<float> result;
        if (accessor.count > std::numeric_limits<std::size_t>::max() / components)
            fail(ImportStatus::resource_limit, DiagnosticCode::source_too_large, std::string(location), "accessor element count is too large");
        result.resize(static_cast<std::size_t>(accessor.count) * components);
        const std::size_t scalar_size = component_size(accessor.component_type, location);
        for (std::uint64_t element = 0; element < accessor.count; ++element)
        {
            const std::byte* element_data = data + static_cast<std::size_t>(element * stride);
            for (std::size_t component = 0; component < components; ++component)
            {
                const double decoded = decode_component(element_data + component * scalar_size, accessor.component_type, accessor.normalized);
                const float value = static_cast<float>(decoded);
                if (!std::isfinite(value))
                    fail(ImportStatus::invalid_source, DiagnosticCode::non_finite_data,
                         std::string(location) + ".element[" + std::to_string(element) + "]",
                         "accessor contains a non-finite value");
                result[static_cast<std::size_t>(element) * components + component] = value;
            }
        }
        static_cast<void>(view); static_cast<void>(element_size);
        return result;
    }

    [[nodiscard]] std::vector<std::uint32_t> read_indices(std::size_t accessor_index, std::string_view location) const
    {
        const auto [accessor, view, data, stride, element_size] = resolve(accessor_index, location);
        if (accessor.type != "SCALAR" || (accessor.component_type != 5121 && accessor.component_type != 5123 && accessor.component_type != 5125) || accessor.normalized)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_accessor, std::string(location),
                 "index accessor must be non-normalized unsigned SCALAR with 8, 16, or 32-bit components");
        std::vector<std::uint32_t> result(static_cast<std::size_t>(accessor.count));
        for (std::uint64_t element = 0; element < accessor.count; ++element)
        {
            const double decoded = decode_component(data + static_cast<std::size_t>(element * stride), accessor.component_type, false);
            result[static_cast<std::size_t>(element)] = static_cast<std::uint32_t>(decoded);
        }
        static_cast<void>(view); static_cast<void>(element_size);
        return result;
    }

private:
    struct Resolved
    {
        const AccessorData& accessor;
        const BufferViewData& view;
        const std::byte* data;
        std::uint64_t stride;
        std::uint64_t element_size;
    };

    [[nodiscard]] Resolved resolve(std::size_t accessor_index, std::string_view location) const
    {
        if (accessor_index >= accessors_.size())
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, std::string(location), "accessor index is out of range");
        const AccessorData& accessor = accessors_[accessor_index];
        if (accessor.sparse)
            fail(ImportStatus::unsupported_feature, DiagnosticCode::unsupported_sparse_accessor, std::string(location), "sparse accessors are explicitly unsupported in Campaign B");
        if (!accessor.buffer_view.has_value() || *accessor.buffer_view >= views_.size())
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_accessor, std::string(location), "accessor does not reference a valid bufferView");
        const BufferViewData& view = views_[*accessor.buffer_view];
        if (view.buffer >= buffers_.size())
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, std::string(location), "bufferView references an invalid buffer");
        const std::uint64_t element_size = checked_multiply(component_size(accessor.component_type, location), component_count(accessor.type, location), location);
        const std::uint64_t stride = view.stride == 0 ? element_size : view.stride;
        if (stride < element_size || stride > 252 || (stride % component_size(accessor.component_type, location)) != 0)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_stride, std::string(location),
                 "bufferView byteStride is invalid for the accessor", "stride >= element size, <= 252, aligned to component size", std::to_string(stride));
        const std::uint64_t occupied = accessor.count == 0 ? 0 : checked_add(checked_multiply(accessor.count - 1, stride, location), element_size, location);
        const std::uint64_t end_in_view = checked_add(accessor.offset, occupied, location);
        if (end_in_view > view.length)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_accessor, std::string(location), "accessor byte range exceeds its bufferView");
        const std::uint64_t absolute = checked_add(view.offset, accessor.offset, location);
        if (absolute > buffers_[view.buffer].bytes.size() || occupied > buffers_[view.buffer].bytes.size() - absolute)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_buffer_range, std::string(location), "accessor byte range exceeds its buffer");
        return {accessor, view, buffers_[view.buffer].bytes.data() + static_cast<std::size_t>(absolute), stride, element_size};
    }

    const std::vector<BufferData>& buffers_;
    const std::vector<BufferViewData>& views_;
    const std::vector<AccessorData>& accessors_;
};

void validate_mesh_attribute_accessor(const AccessorData& accessor,
                                      std::string_view semantic,
                                      bool mesh_quantization_enabled,
                                      std::string_view location)
{
    const auto reject = [&](std::string expected)
    {
        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_accessor, std::string(location),
             "accessor component type, shape, or normalization is invalid for mesh attribute " + std::string(semantic),
             std::move(expected),
             "type=" + accessor.type + ";componentType=" + std::to_string(accessor.component_type) +
                 ";normalized=" + (accessor.normalized ? "true" : "false"));
    };
    const bool floating = accessor.component_type == 5126 && !accessor.normalized;
    if (semantic == "POSITION")
    {
        const bool quantized = mesh_quantization_enabled &&
            (accessor.component_type == 5120 || accessor.component_type == 5121 ||
             accessor.component_type == 5122 || accessor.component_type == 5123);
        if (accessor.type != "VEC3" || (!floating && !quantized))
            reject("VEC3 FLOAT, or KHR_mesh_quantization 8/16-bit integer VEC3");
        return;
    }
    if (semantic == "NORMAL")
    {
        const bool quantized = mesh_quantization_enabled && accessor.normalized &&
            (accessor.component_type == 5120 || accessor.component_type == 5122);
        if (accessor.type != "VEC3" || (!floating && !quantized))
            reject("VEC3 FLOAT, or KHR_mesh_quantization normalized BYTE/SHORT VEC3");
        return;
    }
    if (semantic == "TANGENT")
    {
        const bool quantized = mesh_quantization_enabled && accessor.normalized &&
            (accessor.component_type == 5120 || accessor.component_type == 5122);
        if (accessor.type != "VEC4" || (!floating && !quantized))
            reject("VEC4 FLOAT, or KHR_mesh_quantization normalized BYTE/SHORT VEC4");
        return;
    }
    if (semantic.starts_with("TEXCOORD_"))
    {
        const bool core_integer = accessor.normalized &&
            (accessor.component_type == 5121 || accessor.component_type == 5123);
        const bool quantized = mesh_quantization_enabled &&
            (accessor.component_type == 5120 || accessor.component_type == 5121 ||
             accessor.component_type == 5122 || accessor.component_type == 5123);
        if (accessor.type != "VEC2" || (!floating && !core_integer && !quantized))
            reject("VEC2 FLOAT or normalized UNSIGNED_BYTE/UNSIGNED_SHORT; signed/unscaled integers require KHR_mesh_quantization");
        return;
    }
    if (semantic == "COLOR_0")
    {
        const bool core_integer = accessor.normalized &&
            (accessor.component_type == 5121 || accessor.component_type == 5123);
        if ((accessor.type != "VEC3" && accessor.type != "VEC4") || (!floating && !core_integer))
            reject("VEC3/VEC4 FLOAT or normalized UNSIGNED_BYTE/UNSIGNED_SHORT");
        return;
    }
}

[[nodiscard]] bool in_unit_interval(float value) noexcept
{
    return value >= 0.0F && value <= 1.0F;
}

[[nodiscard]] std::uint32_t read_big_endian_u32(std::span<const std::byte> bytes, std::size_t offset)
{
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) |
           static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] std::uint32_t png_crc32(std::span<const std::byte> bytes) noexcept
{
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const std::byte byte : bytes)
    {
        crc ^= static_cast<std::uint32_t>(byte);
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

struct JpegHuffmanTable
{
    bool defined = false;
    std::array<std::int32_t, 17> minimum_code{};
    std::array<std::int32_t, 17> maximum_code{};
    std::array<std::int32_t, 17> value_offset{};
    std::array<std::uint8_t, 256> values{};
    std::size_t value_count = 0;
};

struct JpegFrameComponent
{
    std::uint8_t id = 0;
    std::uint8_t horizontal_sampling = 0;
    std::uint8_t vertical_sampling = 0;
};

struct JpegBaselineFrame
{
    bool present = false;
    bool eight_bit = false;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t maximum_horizontal_sampling = 1;
    std::uint8_t maximum_vertical_sampling = 1;
    std::array<JpegFrameComponent, 4> components{};
    std::size_t component_count = 0;
};

[[nodiscard]] std::uint16_t read_big_endian_u16(std::span<const std::byte> bytes, std::size_t offset)
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                      static_cast<std::uint16_t>(bytes[offset + 1U]));
}

[[nodiscard]] std::uint64_t ceil_divide(std::uint64_t numerator, std::uint64_t denominator)
{
    return numerator / denominator + (numerator % denominator == 0U ? 0U : 1U);
}

class JpegEntropyReader final
{
public:
    JpegEntropyReader(std::span<const std::byte> bytes, std::size_t offset, std::string_view location)
        : bytes_(bytes), offset_(offset), location_(location)
    {
    }

    [[nodiscard]] std::uint8_t read_bit()
    {
        if (bits_remaining_ == 0U)
        {
            if (offset_ >= bytes_.size())
                reject("JPEG entropy stream ends before all expected MCU blocks are decoded");
            current_byte_ = static_cast<std::uint8_t>(bytes_[offset_++]);
            if (current_byte_ == 0xFFU)
            {
                if (offset_ >= bytes_.size())
                    reject("JPEG entropy stream ends after an unterminated 0xFF byte");
                const std::uint8_t following = static_cast<std::uint8_t>(bytes_[offset_]);
                if (following != 0x00U)
                    reject("JPEG entropy stream reaches a marker before all expected MCU blocks are decoded");
                ++offset_;
                current_byte_ = 0xFFU;
            }
            bits_remaining_ = 8U;
        }
        --bits_remaining_;
        return static_cast<std::uint8_t>((current_byte_ >> bits_remaining_) & 1U);
    }

    void discard_bits(std::uint8_t count)
    {
        for (std::uint8_t bit = 0; bit < count; ++bit) (void)read_bit();
    }

    void consume_restart_marker(std::uint8_t expected_marker)
    {
        align_to_marker();
        if (offset_ >= bytes_.size() || bytes_[offset_] != std::byte{0xFF})
            reject("JPEG restart interval is missing its restart marker");
        while (offset_ < bytes_.size() && bytes_[offset_] == std::byte{0xFF}) ++offset_;
        if (offset_ >= bytes_.size()) reject("JPEG restart marker is truncated");
        const std::uint8_t marker = static_cast<std::uint8_t>(bytes_[offset_++]);
        if (marker != static_cast<std::uint8_t>(0xD0U + expected_marker))
            reject("JPEG restart markers are missing or out of sequence");
    }

    [[nodiscard]] std::size_t finish_scan()
    {
        align_to_marker();
        if (offset_ >= bytes_.size() || bytes_[offset_] != std::byte{0xFF})
            reject("JPEG contains extra entropy bytes after the expected MCU count");

        std::size_t cursor = offset_;
        while (cursor < bytes_.size() && bytes_[cursor] == std::byte{0xFF}) ++cursor;
        if (cursor >= bytes_.size()) reject("JPEG scan terminates with an incomplete marker");
        const std::uint8_t marker = static_cast<std::uint8_t>(bytes_[cursor]);
        if (marker == 0x00U || (marker >= 0xD0U && marker <= 0xD7U))
            reject("JPEG contains entropy or a restart marker after the expected MCU count");
        return offset_;
    }

private:
    void align_to_marker()
    {
        if (bits_remaining_ != 0U)
        {
            const std::uint16_t mask = static_cast<std::uint16_t>((1U << bits_remaining_) - 1U);
            if ((static_cast<std::uint16_t>(current_byte_) & mask) != mask)
                reject("JPEG entropy padding bits are not all set before a marker");
            bits_remaining_ = 0U;
        }
    }

    [[noreturn]] void reject(std::string message) const
    {
        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
             std::string(location_), std::move(message));
    }

    std::span<const std::byte> bytes_;
    std::size_t offset_ = 0;
    std::string_view location_;
    std::uint8_t current_byte_ = 0;
    std::uint8_t bits_remaining_ = 0;
};

[[nodiscard]] std::uint8_t decode_jpeg_huffman_symbol(
    JpegEntropyReader& reader,
    const JpegHuffmanTable& table,
    std::string_view location)
{
    if (!table.defined)
        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
             std::string(location), "JPEG scan references an undefined Huffman table");

    std::int32_t code = 0;
    for (std::size_t length = 1; length <= 16U; ++length)
    {
        code = (code << 1) | static_cast<std::int32_t>(reader.read_bit());
        if (table.maximum_code[length] < 0 || code < table.minimum_code[length] || code > table.maximum_code[length])
            continue;
        const std::int32_t index = table.value_offset[length] + code - table.minimum_code[length];
        if (index < 0 || static_cast<std::size_t>(index) >= table.value_count)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                 std::string(location), "JPEG Huffman table resolves outside its symbol range");
        return table.values[static_cast<std::size_t>(index)];
    }
    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
         std::string(location), "JPEG entropy contains an invalid Huffman code");
}

void decode_baseline_jpeg_block(
    JpegEntropyReader& reader,
    const JpegHuffmanTable& dc_table,
    const JpegHuffmanTable& ac_table,
    std::string_view location)
{
    const std::uint8_t dc_bits = decode_jpeg_huffman_symbol(reader, dc_table, location);
    if (dc_bits > 11U)
        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
             std::string(location), "baseline JPEG DC coefficient category exceeds 11 bits");
    reader.discard_bits(dc_bits);

    std::uint32_t coefficient = 1U;
    while (coefficient <= 63U)
    {
        const std::uint8_t symbol = decode_jpeg_huffman_symbol(reader, ac_table, location);
        const std::uint8_t zero_run = static_cast<std::uint8_t>(symbol >> 4U);
        const std::uint8_t value_bits = static_cast<std::uint8_t>(symbol & 0x0FU);
        if (value_bits == 0U)
        {
            if (zero_run == 0U) return;
            if (zero_run != 15U)
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                     std::string(location), "baseline JPEG AC zero-size symbol is invalid");
            coefficient += 16U;
            if (coefficient > 64U)
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                     std::string(location), "baseline JPEG AC zero run exceeds the block boundary");
            continue;
        }
        if (value_bits > 10U)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                 std::string(location), "baseline JPEG AC coefficient category exceeds 10 bits");
        coefficient += zero_run;
        if (coefficient > 63U)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                 std::string(location), "baseline JPEG AC run exceeds the block boundary");
        reader.discard_bits(value_bits);
        ++coefficient;
    }
}

[[nodiscard]] const JpegFrameComponent* find_jpeg_component(
    const JpegBaselineFrame& frame,
    std::uint8_t id) noexcept
{
    for (std::size_t index = 0; index < frame.component_count; ++index)
    {
        if (frame.components[index].id == id) return &frame.components[index];
    }
    return nullptr;
}

void parse_jpeg_huffman_tables(
    std::span<const std::byte> segment,
    std::array<JpegHuffmanTable, 4>& dc_tables,
    std::array<JpegHuffmanTable, 4>& ac_tables,
    std::string_view location)
{
    std::size_t cursor = 0;
    while (cursor < segment.size())
    {
        if (segment.size() - cursor < 17U)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                 std::string(location), "JPEG DHT segment is truncated");
        const std::uint8_t selector = static_cast<std::uint8_t>(segment[cursor++]);
        const std::uint8_t table_class = static_cast<std::uint8_t>(selector >> 4U);
        const std::uint8_t table_id = static_cast<std::uint8_t>(selector & 0x0FU);
        if (table_class > 1U || table_id >= 4U)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                 std::string(location), "JPEG DHT table class or identifier is invalid");

        std::array<std::uint8_t, 16> counts{};
        std::size_t symbol_count = 0;
        for (std::size_t length = 0; length < counts.size(); ++length)
        {
            counts[length] = static_cast<std::uint8_t>(segment[cursor++]);
            symbol_count += counts[length];
        }
        if (symbol_count == 0U || symbol_count > 256U || segment.size() - cursor < symbol_count)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                 std::string(location), "JPEG DHT symbol payload is invalid or truncated");

        JpegHuffmanTable table;
        table.defined = true;
        table.value_count = symbol_count;
        table.minimum_code.fill(-1);
        table.maximum_code.fill(-1);
        table.value_offset.fill(0);
        for (std::size_t symbol_index = 0; symbol_index < symbol_count; ++symbol_index)
            table.values[symbol_index] = static_cast<std::uint8_t>(segment[cursor + symbol_index]);
        cursor += symbol_count;

        std::int32_t code = 0;
        std::int32_t value_index = 0;
        for (std::size_t length = 1; length <= 16U; ++length)
        {
            const std::int32_t count = counts[length - 1U];
            const std::int32_t code_space = static_cast<std::int32_t>(1U << length);
            if (code + count > code_space)
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                     std::string(location), "JPEG DHT describes an oversubscribed Huffman tree");
            if (count > 0)
            {
                table.minimum_code[length] = code;
                table.maximum_code[length] = code + count - 1;
                table.value_offset[length] = value_index;
            }
            code = (code + count) << 1;
            value_index += count;
        }

        if (table_class == 0U) dc_tables[table_id] = table;
        else ac_tables[table_id] = table;
    }
}

[[nodiscard]] std::size_t find_next_jpeg_marker(
    std::span<const std::byte> bytes,
    std::size_t scan_start,
    std::string_view location)
{
    std::size_t cursor = scan_start;
    while (cursor < bytes.size())
    {
        if (bytes[cursor] != std::byte{0xFF})
        {
            ++cursor;
            continue;
        }
        const std::size_t marker_offset = cursor;
        ++cursor;
        while (cursor < bytes.size() && bytes[cursor] == std::byte{0xFF}) ++cursor;
        if (cursor >= bytes.size())
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                 std::string(location), "JPEG scan ends with an incomplete marker");
        const std::uint8_t marker = static_cast<std::uint8_t>(bytes[cursor]);
        if (marker == 0x00U)
        {
            ++cursor;
            continue;
        }
        if (marker >= 0xD0U && marker <= 0xD7U)
        {
            ++cursor;
            continue;
        }
        return marker_offset;
    }
    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
         std::string(location), "JPEG scan does not terminate with a marker");
}

void validate_baseline_jpeg_entropy(
    std::span<const std::byte> bytes,
    std::string_view location)
{
    if (bytes.size() < 4U || bytes[0] != std::byte{0xFF} || bytes[1] != std::byte{0xD8}) return;

    JpegBaselineFrame frame;
    std::array<JpegHuffmanTable, 4> dc_tables{};
    std::array<JpegHuffmanTable, 4> ac_tables{};
    std::uint16_t restart_interval = 0;
    std::size_t offset = 2U;

    while (offset < bytes.size())
    {
        if (bytes[offset] != std::byte{0xFF})
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                 std::string(location), "JPEG marker prefix is missing outside entropy data");
        while (offset < bytes.size() && bytes[offset] == std::byte{0xFF}) ++offset;
        if (offset >= bytes.size())
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                 std::string(location), "JPEG terminates with an incomplete marker");
        const std::uint8_t marker = static_cast<std::uint8_t>(bytes[offset++]);
        if (marker == 0xD9U) return;
        if (marker == 0xD8U || marker == 0x01U || (marker >= 0xD0U && marker <= 0xD7U)) continue;
        if (offset + 2U > bytes.size())
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                 std::string(location), "JPEG segment length is truncated");
        const std::uint16_t length = read_big_endian_u16(bytes, offset);
        if (length < 2U || offset + length > bytes.size())
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                 std::string(location), "JPEG segment length is invalid");
        const std::size_t payload_offset = offset + 2U;
        const std::size_t payload_size = static_cast<std::size_t>(length - 2U);
        const std::span<const std::byte> payload = bytes.subspan(payload_offset, payload_size);

        if (marker == 0xC0U)
        {
            if (payload.size() < 6U)
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                     std::string(location), "baseline JPEG frame header is truncated");
            const std::uint8_t precision = static_cast<std::uint8_t>(payload[0]);
            const std::uint8_t component_count = static_cast<std::uint8_t>(payload[5]);
            if (component_count == 0U || component_count > frame.components.size() ||
                payload.size() != 6U + static_cast<std::size_t>(component_count) * 3U)
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                     std::string(location), "baseline JPEG frame component table is invalid");

            frame.present = true;
            frame.eight_bit = precision == 8U;
            frame.height = read_big_endian_u16(payload, 1U);
            frame.width = read_big_endian_u16(payload, 3U);
            frame.component_count = component_count;
            frame.maximum_horizontal_sampling = 1U;
            frame.maximum_vertical_sampling = 1U;
            for (std::size_t component_index = 0; component_index < frame.component_count; ++component_index)
            {
                const std::size_t component_offset = 6U + component_index * 3U;
                JpegFrameComponent component;
                component.id = static_cast<std::uint8_t>(payload[component_offset]);
                const std::uint8_t sampling = static_cast<std::uint8_t>(payload[component_offset + 1U]);
                component.horizontal_sampling = static_cast<std::uint8_t>(sampling >> 4U);
                component.vertical_sampling = static_cast<std::uint8_t>(sampling & 0x0FU);
                if (component.horizontal_sampling == 0U || component.horizontal_sampling > 4U ||
                    component.vertical_sampling == 0U || component.vertical_sampling > 4U)
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                         std::string(location), "baseline JPEG sampling factor is outside the supported JPEG range");
                frame.maximum_horizontal_sampling = std::max(frame.maximum_horizontal_sampling, component.horizontal_sampling);
                frame.maximum_vertical_sampling = std::max(frame.maximum_vertical_sampling, component.vertical_sampling);
                frame.components[component_index] = component;
            }
        }
        else if (marker == 0xC2U)
        {
            // Progressive JPEG remains supported through the full decoder backend.  The
            // project-owned entropy walk below is intentionally limited to sequential
            // baseline scans because progressive refinement has different coefficient
            // state semantics.  Container/marker validation and full pixel decode still
            // apply to progressive files.
            frame.present = false;
        }
        else if (marker == 0xC4U)
        {
            parse_jpeg_huffman_tables(payload, dc_tables, ac_tables, location);
        }
        else if (marker == 0xDDU)
        {
            if (payload.size() != 2U)
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                     std::string(location), "JPEG DRI segment must contain exactly one restart interval");
            restart_interval = read_big_endian_u16(payload, 0U);
        }
        else if (marker == 0xDAU)
        {
            if (payload.empty())
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                     std::string(location), "JPEG SOS segment is truncated");
            const std::uint8_t scan_component_count = static_cast<std::uint8_t>(payload[0]);
            const std::size_t expected_payload_size = 1U + static_cast<std::size_t>(scan_component_count) * 2U + 3U;
            if (scan_component_count == 0U || scan_component_count > 4U || payload.size() != expected_payload_size)
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                     std::string(location), "JPEG SOS component table is invalid");

            const std::size_t scan_start = offset + length;
            if (!frame.present || !frame.eight_bit)
            {
                offset = find_next_jpeg_marker(bytes, scan_start, location);
                continue;
            }

            const std::uint8_t spectral_start = static_cast<std::uint8_t>(payload[payload.size() - 3U]);
            const std::uint8_t spectral_end = static_cast<std::uint8_t>(payload[payload.size() - 2U]);
            const std::uint8_t approximation = static_cast<std::uint8_t>(payload[payload.size() - 1U]);
            if (spectral_start != 0U || spectral_end != 63U || approximation != 0U)
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                     std::string(location), "baseline JPEG SOS spectral parameters are invalid");

            struct ScanComponent
            {
                const JpegFrameComponent* frame_component = nullptr;
                const JpegHuffmanTable* dc_table = nullptr;
                const JpegHuffmanTable* ac_table = nullptr;
            };
            std::array<ScanComponent, 4> scan_components{};
            for (std::size_t scan_index = 0; scan_index < scan_component_count; ++scan_index)
            {
                const std::uint8_t component_id = static_cast<std::uint8_t>(payload[1U + scan_index * 2U]);
                const std::uint8_t selectors = static_cast<std::uint8_t>(payload[2U + scan_index * 2U]);
                const std::uint8_t dc_id = static_cast<std::uint8_t>(selectors >> 4U);
                const std::uint8_t ac_id = static_cast<std::uint8_t>(selectors & 0x0FU);
                if (dc_id >= dc_tables.size() || ac_id >= ac_tables.size())
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                         std::string(location), "JPEG SOS references an out-of-range Huffman table identifier");
                const JpegFrameComponent* component = find_jpeg_component(frame, component_id);
                if (component == nullptr)
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image,
                         std::string(location), "JPEG SOS references a component absent from the frame header");
                scan_components[scan_index] = {component, &dc_tables[dc_id], &ac_tables[ac_id]};
            }

            std::uint64_t mcu_columns = 0;
            std::uint64_t mcu_rows = 0;
            if (scan_component_count > 1U)
            {
                mcu_columns = ceil_divide(frame.width, static_cast<std::uint64_t>(8U * frame.maximum_horizontal_sampling));
                mcu_rows = ceil_divide(frame.height, static_cast<std::uint64_t>(8U * frame.maximum_vertical_sampling));
            }
            else
            {
                const JpegFrameComponent& component = *scan_components[0].frame_component;
                mcu_columns = ceil_divide(static_cast<std::uint64_t>(frame.width) * component.horizontal_sampling,
                                          static_cast<std::uint64_t>(8U * frame.maximum_horizontal_sampling));
                mcu_rows = ceil_divide(static_cast<std::uint64_t>(frame.height) * component.vertical_sampling,
                                       static_cast<std::uint64_t>(8U * frame.maximum_vertical_sampling));
            }
            const std::uint64_t mcu_count = mcu_columns * mcu_rows;
            JpegEntropyReader reader(bytes, scan_start, location);
            std::uint8_t expected_restart = 0U;
            for (std::uint64_t mcu = 0; mcu < mcu_count; ++mcu)
            {
                for (std::size_t scan_index = 0; scan_index < scan_component_count; ++scan_index)
                {
                    const ScanComponent& component = scan_components[scan_index];
                    const std::uint32_t blocks = scan_component_count > 1U
                        ? static_cast<std::uint32_t>(component.frame_component->horizontal_sampling) *
                          static_cast<std::uint32_t>(component.frame_component->vertical_sampling)
                        : 1U;
                    for (std::uint32_t block = 0; block < blocks; ++block)
                        decode_baseline_jpeg_block(reader, *component.dc_table, *component.ac_table, location);
                }
                if (restart_interval != 0U && (mcu + 1U) % restart_interval == 0U && mcu + 1U < mcu_count)
                {
                    reader.consume_restart_marker(expected_restart);
                    expected_restart = static_cast<std::uint8_t>((expected_restart + 1U) & 7U);
                }
            }
            offset = reader.finish_scan();
            continue;
        }

        offset += length;
    }
}

[[nodiscard]] std::tuple<std::uint32_t, std::uint32_t, std::uint32_t> parse_image_metadata(
    std::span<const std::byte> bytes,
    std::string_view mime_hint,
    std::string_view location,
    std::string& detected_mime)
{
    constexpr std::array<std::byte, 8> png_signature{
        std::byte{0x89}, std::byte{'P'}, std::byte{'N'}, std::byte{'G'}, std::byte{0x0D}, std::byte{0x0A}, std::byte{0x1A}, std::byte{0x0A}};
    if (bytes.size() >= png_signature.size() && std::equal(png_signature.begin(), png_signature.end(), bytes.begin()))
    {
        std::size_t offset = png_signature.size();
        bool saw_header = false;
        bool saw_data = false;
        bool saw_end = false;
        std::array<std::byte, 2> zlib_header{};
        std::size_t zlib_header_size = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t components = 0;
        while (offset < bytes.size())
        {
            if (bytes.size() - offset < 12U)
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "PNG chunk header is truncated");
            const std::uint32_t length = read_big_endian_u32(bytes, offset);
            const std::uint64_t chunk_end_u64 = checked_add(offset, checked_add(12U, length, location), location);
            if (chunk_end_u64 > bytes.size())
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "PNG chunk extends beyond the encoded payload");
            const std::size_t chunk_end = static_cast<std::size_t>(chunk_end_u64);
            const std::span<const std::byte> type_and_data = bytes.subspan(offset + 4U, 4U + length);
            const std::uint32_t expected_crc = read_big_endian_u32(bytes, offset + 8U + length);
            const std::uint32_t observed_crc = png_crc32(type_and_data);
            if (expected_crc != observed_crc)
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "PNG chunk CRC mismatch");
            const std::string type(reinterpret_cast<const char*>(bytes.data() + offset + 4U), 4U);
            if (!saw_header)
            {
                if (type != "IHDR" || length != 13U)
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "PNG must begin with a 13-byte IHDR chunk");
                width = read_big_endian_u32(bytes, offset + 8U);
                height = read_big_endian_u32(bytes, offset + 12U);
                const std::uint8_t bit_depth = static_cast<std::uint8_t>(bytes[offset + 16U]);
                const std::uint8_t color_type = static_cast<std::uint8_t>(bytes[offset + 17U]);
                const std::uint8_t compression = static_cast<std::uint8_t>(bytes[offset + 18U]);
                const std::uint8_t filter = static_cast<std::uint8_t>(bytes[offset + 19U]);
                const std::uint8_t interlace = static_cast<std::uint8_t>(bytes[offset + 20U]);
                const bool legal_depth =
                    (color_type == 0U && (bit_depth == 1U || bit_depth == 2U || bit_depth == 4U || bit_depth == 8U || bit_depth == 16U)) ||
                    (color_type == 2U && (bit_depth == 8U || bit_depth == 16U)) ||
                    (color_type == 3U && (bit_depth == 1U || bit_depth == 2U || bit_depth == 4U || bit_depth == 8U)) ||
                    (color_type == 4U && (bit_depth == 8U || bit_depth == 16U)) ||
                    (color_type == 6U && (bit_depth == 8U || bit_depth == 16U));
                if (width == 0 || height == 0 || !legal_depth || compression != 0U || filter != 0U || interlace > 1U)
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "PNG IHDR fields are invalid or unsupported");
                switch (color_type)
                {
                case 0: components = 1; break;
                case 2: components = 3; break;
                case 3: components = 3; break;
                case 4: components = 2; break;
                case 6: components = 4; break;
                default: break;
                }
                saw_header = true;
            }
            else if (type == "IHDR")
            {
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "PNG contains more than one IHDR chunk");
            }
            if (type == "IDAT")
            {
                saw_data = true;
                const std::size_t needed = zlib_header.size() - zlib_header_size;
                const std::size_t available = static_cast<std::size_t>(length);
                const std::size_t copy_count = std::min(needed, available);
                if (copy_count > 0U)
                {
                    std::copy_n(bytes.begin() + static_cast<std::ptrdiff_t>(offset + 8U),
                                static_cast<std::ptrdiff_t>(copy_count),
                                zlib_header.begin() + static_cast<std::ptrdiff_t>(zlib_header_size));
                    zlib_header_size += copy_count;
                }
            }
            if (type == "IEND")
            {
                if (length != 0U || !saw_data)
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "PNG IEND is invalid or appears before image data");
                saw_end = true;
                offset = chunk_end;
                break;
            }
            offset = chunk_end;
        }
        if (!saw_header || !saw_data || !saw_end || offset != bytes.size())
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "PNG is incomplete or has trailing bytes after IEND");
        if (zlib_header_size != zlib_header.size())
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "PNG IDAT stream is too short to contain a zlib header");

        const std::uint8_t cmf = static_cast<std::uint8_t>(zlib_header[0]);
        const std::uint8_t flg = static_cast<std::uint8_t>(zlib_header[1]);
        const std::uint16_t zlib_header_word = static_cast<std::uint16_t>((static_cast<std::uint16_t>(cmf) << 8U) | flg);
        const bool deflate_method = (cmf & 0x0FU) == 8U;
        const bool valid_window = (cmf >> 4U) <= 7U;
        const bool valid_check_bits = (zlib_header_word % 31U) == 0U;
        const bool uses_preset_dictionary = (flg & 0x20U) != 0U;
        if (!deflate_method || !valid_window || !valid_check_bits || uses_preset_dictionary)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location),
                 "PNG IDAT stream does not begin with a valid PNG zlib/DEFLATE header");

        detected_mime = "image/png";
        return {width, height, components};
    }

    if (bytes.size() >= 4 && bytes[0] == std::byte{0xFF} && bytes[1] == std::byte{0xD8})
    {
        validate_baseline_jpeg_entropy(bytes, location);
        std::size_t offset = 2;
        bool saw_frame = false;
        bool saw_scan = false;
        bool saw_end = false;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::uint32_t components = 0;
        while (offset < bytes.size())
        {
            if (bytes[offset] != std::byte{0xFF})
            {
                if (!saw_scan)
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "JPEG marker prefix is missing");
                ++offset;
                continue;
            }
            while (offset < bytes.size() && bytes[offset] == std::byte{0xFF}) ++offset;
            if (offset >= bytes.size()) break;
            const std::uint8_t marker = static_cast<std::uint8_t>(bytes[offset++]);
            if (marker == 0x00U)
            {
                if (!saw_scan) fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "JPEG stuffed byte appears outside scan data");
                continue;
            }
            if (marker == 0xD9U) { saw_end = true; break; }
            if (marker == 0xD8U || marker == 0x01U || (marker >= 0xD0U && marker <= 0xD7U)) continue;
            if (offset + 2U > bytes.size())
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "JPEG segment length is truncated");
            const std::uint16_t length = static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                                                    static_cast<std::uint16_t>(bytes[offset + 1U]));
            if (length < 2U || offset + length > bytes.size())
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "JPEG segment length is invalid");
            const bool start_of_frame = (marker >= 0xC0U && marker <= 0xC3U) || (marker >= 0xC5U && marker <= 0xC7U) ||
                                        (marker >= 0xC9U && marker <= 0xCBU) || (marker >= 0xCDU && marker <= 0xCFU);
            if (start_of_frame)
            {
                if (length < 8U)
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "JPEG frame header is too short");
                height = (static_cast<std::uint32_t>(bytes[offset + 3U]) << 8U) | static_cast<std::uint32_t>(bytes[offset + 4U]);
                width = (static_cast<std::uint32_t>(bytes[offset + 5U]) << 8U) | static_cast<std::uint32_t>(bytes[offset + 6U]);
                components = static_cast<std::uint8_t>(bytes[offset + 7U]);
                if (width == 0 || height == 0 || (components != 1U && components != 3U))
                    fail(ImportStatus::unsupported_feature, DiagnosticCode::unsupported_image_encoding, std::string(location),
                         "Campaign B supports grayscale and three-component JPEG images only");
                saw_frame = true;
            }
            if (marker == 0xDAU) saw_scan = true;
            offset += length;
        }
        if (!saw_frame || !saw_scan || !saw_end)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, std::string(location), "JPEG is missing a frame, scan, or end marker");
        detected_mime = "image/jpeg";
        return {width, height, components};
    }
    fail(ImportStatus::unsupported_feature, DiagnosticCode::unsupported_image_encoding, std::string(location),
         "unsupported image encoding", "PNG or JPEG", std::string(mime_hint));
}

[[nodiscard]] FilterMode parse_filter(std::optional<std::uint64_t> value, std::string_view location)
{
    if (!value.has_value()) return FilterMode::unspecified;
    switch (*value)
    {
    case 9728: return FilterMode::nearest;
    case 9729: return FilterMode::linear;
    case 9984: return FilterMode::nearest_mipmap_nearest;
    case 9985: return FilterMode::linear_mipmap_nearest;
    case 9986: return FilterMode::nearest_mipmap_linear;
    case 9987: return FilterMode::linear_mipmap_linear;
    default: fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, std::string(location), "invalid glTF sampler filter value", {}, std::to_string(*value));
    }
}

[[nodiscard]] WrapMode parse_wrap(std::optional<std::uint64_t> value, std::string_view location)
{
    const std::uint64_t actual = value.value_or(10497);
    switch (actual)
    {
    case 33071: return WrapMode::clamp_to_edge;
    case 33648: return WrapMode::mirrored_repeat;
    case 10497: return WrapMode::repeat;
    default: fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, std::string(location), "invalid glTF sampler wrap value", {}, std::to_string(actual));
    }
}

[[nodiscard]] std::optional<TextureReference> parse_texture_reference(const JsonValue::Object& owner,
                                                                      std::string_view key,
                                                                      std::size_t texture_count,
                                                                      std::vector<Diagnostic>& diagnostics,
                                                                      std::string_view location,
                                                                      float* scalar = nullptr)
{
    const JsonValue::Object* info = optional_object(owner, key, location);
    if (info == nullptr) return std::nullopt;
    const auto index = optional_uint(*info, "index", std::string(location) + "." + std::string(key));
    if (!index.has_value() || *index >= texture_count)
        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, std::string(location) + "." + std::string(key) + ".index", "texture index is missing or out of range");
    TextureReference reference;
    reference.texture = TextureId(static_cast<std::uint32_t>(*index));
    reference.texcoord_set = static_cast<std::uint32_t>(optional_uint(*info, "texCoord", location).value_or(0));
    if (reference.texcoord_set > 1)
        fail(ImportStatus::unsupported_feature, DiagnosticCode::unsupported_accessor_shape, std::string(location) + "." + std::string(key) + ".texCoord", "only TEXCOORD_0 and TEXCOORD_1 are supported");
    if (scalar != nullptr)
    {
        const auto scalar_iterator = info->find(key == "normalTexture" ? "scale" : "strength");
        if (scalar_iterator != info->end()) *scalar = number_to_float(scalar_iterator->second, location);
        reference.scale = *scalar;
    }
    if (const JsonValue::Object* extensions = optional_object(*info, "extensions", location); extensions != nullptr && extensions->contains("KHR_texture_transform"))
    {
        add_warning(diagnostics, DiagnosticCode::texture_transform_unsupported, DiagnosticDisposition::ignored,
                    std::string(location) + "." + std::string(key) + ".extensions.KHR_texture_transform",
                    "KHR_texture_transform is not supported in Campaign B and was ignored");
    }
    return reference;
}

[[nodiscard]] ImportStatus status_from_diagnostics(const std::vector<Diagnostic>& diagnostics)
{
    bool warning = false;
    bool repair = false;
    for (const Diagnostic& diagnostic : diagnostics)
    {
        warning |= diagnostic.severity == DiagnosticSeverity::warning;
        repair |= diagnostic.disposition == DiagnosticDisposition::repaired || diagnostic.disposition == DiagnosticDisposition::defaulted;
    }
    if (repair) return ImportStatus::success_with_repairs;
    if (warning) return ImportStatus::success_with_warnings;
    return ImportStatus::success;
}

[[nodiscard]] std::string compose_asset_key(const CanonicalScene& scene, std::string_view settings)
{
    Sha256 hash;
    hash.update(CanonicalScene::schema_version);
    hash.update("\n");
    hash.update(settings);
    hash.update("\nsource:");
    hash.update(scene.source.source_sha256);
    std::vector<DependencyRecord> dependencies = scene.source.dependencies;
    std::sort(dependencies.begin(), dependencies.end(), [](const auto& left, const auto& right) { return left.normalized_relative_path < right.normalized_relative_path; });
    for (const DependencyRecord& dependency : dependencies)
    {
        hash.update("\ndep:"); hash.update(dependency.normalized_relative_path); hash.update(":"); hash.update(dependency.sha256);
    }
    const auto digest = hash.finish();
    constexpr char hex[] = "0123456789abcdef";
    std::string output(64, '0');
    for (std::size_t index = 0; index < digest.size(); ++index)
    {
        const unsigned value = static_cast<unsigned>(digest[index]);
        output[index * 2] = hex[value >> 4U]; output[index * 2 + 1] = hex[value & 0x0FU];
    }
    return output;
}
}

std::string ImportSettings::deterministic_description() const
{
    std::ostringstream output;
    output << "scene=" << scene_selector.value_or("<default>")
           << ";max_source=" << maximum_source_bytes
           << ";max_dependency=" << maximum_dependency_bytes
           << ";max_total=" << maximum_total_decoded_bytes
           << ";max_peak=" << maximum_peak_bytes
           << ";reject_path_traversal=" << (reject_path_traversal ? "true" : "false")
           << ";sparse_accessors=false;primitive_modes=triangles;images=png,jpeg";
    return output.str();
}

bool ImportResult::succeeded() const noexcept
{
    return is_success(report.status);
}

ImportResult GltfImporter::import_file(const std::filesystem::path& source, const ImportSettings& settings) const
{
    CanonicalScene scene;
    std::vector<Diagnostic> diagnostics;
    ImportStatus final_status = ImportStatus::internal_error;
    const std::string settings_description = settings.deterministic_description();
    scene.source.display_name = source.filename().generic_string();

    try
    {
        ResourceBudget budget(settings, scene.source.resource_usage);
        const std::uint64_t source_allocation_limit = std::min({settings.maximum_source_bytes,
                                                                settings.maximum_total_decoded_bytes,
                                                                settings.maximum_peak_bytes});
        ParsedDocument document = parse_document(read_file(source, source_allocation_limit, "source"));
        budget.add_source(document.source_bytes.size(), "source");
        if (!document.glb_binary.empty())
            budget.observe_scratch(document.glb_binary.size(), "glb_binary_parse_scratch", "glb.binary");
        scene.default_material.name = "glTF default material";
        scene.source.format = document.format;
        scene.source.source_sha256 = sha256_hex(std::span<const std::byte>(document.source_bytes));
        const auto& root = require_object(document.root, "json");

        const JsonValue::Object* asset = optional_object(root, "asset", "json");
        if (asset == nullptr)
            fail(ImportStatus::invalid_source, DiagnosticCode::unsupported_version, "json.asset", "glTF asset object is required");
        scene.source.version = optional_string(*asset, "version", "json.asset");
        if (scene.source.version != "2.0")
            fail(ImportStatus::unsupported_feature, DiagnosticCode::unsupported_version, "json.asset.version", "only glTF 2.0 is supported", "2.0", scene.source.version);
        scene.source.generator = optional_string(*asset, "generator", "json.asset");
        scene.source.copyright = optional_string(*asset, "copyright", "json.asset");
        scene.source.extensions_used = parse_string_array(root, "extensionsUsed", "json");
        scene.source.extensions_required = parse_string_array(root, "extensionsRequired", "json");
        const std::set<std::string> supported_extensions{"KHR_lights_punctual", "KHR_mesh_quantization"};
        for (const std::string& extension : scene.source.extensions_required)
            if (!supported_extensions.contains(extension))
                fail(ImportStatus::unsupported_feature, DiagnosticCode::unsupported_required_extension,
                     "json.extensionsRequired", "required extension is unsupported", "KHR_lights_punctual or KHR_mesh_quantization", extension);
        for (const std::string& extension : scene.source.extensions_used)
            if (!supported_extensions.contains(extension))
                add_warning(diagnostics, DiagnosticCode::unsupported_optional_extension, DiagnosticDisposition::ignored,
                            "json.extensionsUsed", "optional extension is unsupported and was ignored", {}, extension);
        const bool mesh_quantization_enabled = std::find(scene.source.extensions_required.begin(),
                                                         scene.source.extensions_required.end(),
                                                         "KHR_mesh_quantization") != scene.source.extensions_required.end();
        if (std::find(scene.source.extensions_used.begin(), scene.source.extensions_used.end(), "KHR_mesh_quantization") != scene.source.extensions_used.end() &&
            !mesh_quantization_enabled)
            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, "json.extensionsRequired",
                 "KHR_mesh_quantization must be listed in extensionsRequired when used");

        const std::filesystem::path asset_root = source.parent_path();
        std::vector<BufferData> buffers;
        if (const JsonValue::Array* buffer_array = optional_array(root, "buffers", "json"); buffer_array != nullptr)
        {
            buffers.reserve(buffer_array->size());
            for (std::size_t index = 0; index < buffer_array->size(); ++index)
            {
                const std::string location = "json.buffers[" + std::to_string(index) + "]";
                const auto& object = require_object((*buffer_array)[index], location);
                const std::uint64_t declared_length = optional_uint(object, "byteLength", location).value_or(0);
                BufferData buffer;
                const std::string uri = optional_string(object, "uri", location);
                if (uri.empty())
                {
                    if (document.format != "glb" || index != 0 || document.glb_binary.empty())
                        fail(ImportStatus::missing_dependency, DiagnosticCode::missing_dependency, location + ".uri", "buffer has no URI and no matching GLB BIN chunk");
                    budget.preflight_retain(document.glb_binary.size(), "buffer_payload", location);
                    buffer.bytes = std::move(document.glb_binary);
                    buffer.identity = "<glb-bin>";
                }
                else if (uri.starts_with("data:"))
                {
                    budget.preflight_retain(data_uri_decoded_size(uri, location + ".uri"), "buffer_payload", location);
                    const std::optional<DataUri> data_uri = parse_data_uri(uri, location + ".uri");
                    buffer.bytes = data_uri->bytes;
                    buffer.identity = "<data-uri-buffer-" + std::to_string(index) + ">";
                }
                else
                {
                    std::string relative;
                    const std::filesystem::path dependency = resolve_dependency(asset_root, uri, settings.reject_path_traversal, location + ".uri", relative);
                    std::error_code size_error;
                    const std::uintmax_t dependency_size = std::filesystem::file_size(dependency, size_error);
                    if (!size_error) budget.preflight_retain(dependency_size, "buffer_payload", location);
                    buffer.bytes = read_file(dependency, settings.maximum_dependency_bytes, location + ".uri",
                                             ImportStatus::missing_dependency, DiagnosticCode::missing_dependency);
                    buffer.identity = relative;
                    scene.source.dependencies.push_back({relative, sha256_hex(std::span<const std::byte>(buffer.bytes)), buffer.bytes.size()});
                }
                budget.add_buffer(buffer.bytes.size(), location);
                if (buffer.bytes.size() < declared_length)
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_buffer_range, location + ".byteLength", "buffer contains fewer bytes than declared", std::to_string(declared_length), std::to_string(buffer.bytes.size()));
                buffers.push_back(std::move(buffer));
            }
        }

        std::vector<BufferViewData> views;
        if (const JsonValue::Array* view_array = optional_array(root, "bufferViews", "json"); view_array != nullptr)
        {
            views.reserve(view_array->size());
            for (std::size_t index = 0; index < view_array->size(); ++index)
            {
                const std::string location = "json.bufferViews[" + std::to_string(index) + "]";
                const auto& object = require_object((*view_array)[index], location);
                BufferViewData view;
                view.buffer = static_cast<std::size_t>(optional_uint(object, "buffer", location).value_or(std::numeric_limits<std::uint64_t>::max()));
                view.offset = optional_uint(object, "byteOffset", location).value_or(0);
                view.length = optional_uint(object, "byteLength", location).value_or(0);
                view.stride = optional_uint(object, "byteStride", location).value_or(0);
                if (view.buffer >= buffers.size())
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".buffer", "bufferView references an invalid buffer");
                const std::uint64_t end = checked_add(view.offset, view.length, location);
                if (end > buffers[view.buffer].bytes.size())
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_buffer_range, location, "bufferView byte range exceeds buffer");
                views.push_back(view);
            }
        }

        std::vector<AccessorData> accessors;
        if (const JsonValue::Array* accessor_array = optional_array(root, "accessors", "json"); accessor_array != nullptr)
        {
            accessors.reserve(accessor_array->size());
            for (std::size_t index = 0; index < accessor_array->size(); ++index)
            {
                const std::string location = "json.accessors[" + std::to_string(index) + "]";
                const auto& object = require_object((*accessor_array)[index], location);
                AccessorData accessor;
                if (const auto view = optional_uint(object, "bufferView", location); view.has_value()) accessor.buffer_view = static_cast<std::size_t>(*view);
                accessor.offset = optional_uint(object, "byteOffset", location).value_or(0);
                accessor.count = optional_uint(object, "count", location).value_or(0);
                accessor.component_type = static_cast<std::uint32_t>(optional_uint(object, "componentType", location).value_or(0));
                accessor.type = optional_string(object, "type", location);
                accessor.normalized = optional_bool(object, "normalized", false, location);
                accessor.sparse = object.contains("sparse");
                static_cast<void>(component_size(accessor.component_type, location));
                const std::size_t accessor_components = component_count(accessor.type, location);
                if (const auto iterator = object.find("min"); iterator != object.end())
                    accessor.declared_min = read_numeric_array(iterator->second, accessor_components, location + ".min");
                if (const auto iterator = object.find("max"); iterator != object.end())
                    accessor.declared_max = read_numeric_array(iterator->second, accessor_components, location + ".max");
                if (accessor.declared_min.empty() != accessor.declared_max.empty())
                    add_warning(diagnostics, DiagnosticCode::bounds_recomputed, DiagnosticDisposition::ignored, location,
                                "accessor declared only one bounds endpoint; source bounds were ignored and recomputed");
                if (accessor.buffer_view.has_value() && *accessor.buffer_view >= views.size())
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".bufferView", "accessor references an invalid bufferView");
                accessors.push_back(std::move(accessor));
            }
        }
        const AccessorReader reader(buffers, views, accessors);

        if (const JsonValue::Array* sampler_array = optional_array(root, "samplers", "json"); sampler_array != nullptr)
        {
            for (std::size_t index = 0; index < sampler_array->size(); ++index)
            {
                const std::string location = "json.samplers[" + std::to_string(index) + "]";
                const auto& object = require_object((*sampler_array)[index], location);
                Sampler sampler;
                sampler.name = optional_string(object, "name", location);
                sampler.mag_filter = parse_filter(optional_uint(object, "magFilter", location), location + ".magFilter");
                sampler.min_filter = parse_filter(optional_uint(object, "minFilter", location), location + ".minFilter");
                sampler.wrap_s = parse_wrap(optional_uint(object, "wrapS", location), location + ".wrapS");
                sampler.wrap_t = parse_wrap(optional_uint(object, "wrapT", location), location + ".wrapT");
                scene.samplers.push_back(std::move(sampler));
            }
        }

        if (const JsonValue::Array* image_array = optional_array(root, "images", "json"); image_array != nullptr)
        {
            for (std::size_t index = 0; index < image_array->size(); ++index)
            {
                const std::string location = "json.images[" + std::to_string(index) + "]";
                const auto& object = require_object((*image_array)[index], location);
                Image image;
                image.name = optional_string(object, "name", location);
                image.mime_type = optional_string(object, "mimeType", location);
                const std::string uri = optional_string(object, "uri", location);
                const auto view_index = optional_uint(object, "bufferView", location);
                if (!uri.empty() && view_index.has_value())
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, location, "image may not define both URI and bufferView");
                if (!uri.empty())
                {
                    if (uri.starts_with("data:"))
                    {
                        budget.preflight_retain(data_uri_decoded_size(uri, location + ".uri"), "encoded_image", location);
                        const std::optional<DataUri> data_uri = parse_data_uri(uri, location + ".uri");
                        image.encoded_bytes = data_uri->bytes;
                        image.source_identity = "<data-uri-image-" + std::to_string(index) + ">";
                        if (image.mime_type.empty()) image.mime_type = data_uri->mime_type;
                    }
                    else
                    {
                        std::string relative;
                        const std::filesystem::path dependency = resolve_dependency(asset_root, uri, settings.reject_path_traversal, location + ".uri", relative);
                        std::error_code size_error;
                        const std::uintmax_t dependency_size = std::filesystem::file_size(dependency, size_error);
                        if (!size_error) budget.preflight_retain(dependency_size, "encoded_image", location);
                        image.encoded_bytes = read_file(dependency, settings.maximum_dependency_bytes, location + ".uri",
                                                         ImportStatus::missing_dependency, DiagnosticCode::missing_dependency);
                        image.source_identity = relative;
                        scene.source.dependencies.push_back({relative, sha256_hex(std::span<const std::byte>(image.encoded_bytes)), image.encoded_bytes.size()});
                    }
                }
                else if (view_index.has_value())
                {
                    if (*view_index >= views.size())
                        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".bufferView", "image references an invalid bufferView");
                    const BufferViewData& view = views[static_cast<std::size_t>(*view_index)];
                    budget.preflight_retain(view.length, "encoded_image", location);
                    const auto& buffer = buffers[view.buffer].bytes;
                    image.encoded_bytes.assign(buffer.begin() + static_cast<std::ptrdiff_t>(view.offset),
                                               buffer.begin() + static_cast<std::ptrdiff_t>(view.offset + view.length));
                    image.source_identity = "<bufferView-" + std::to_string(*view_index) + ">";
                    if (image.mime_type.empty())
                        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, location + ".mimeType", "bufferView-backed images require a MIME type");
                }
                else
                {
                    fail(ImportStatus::missing_dependency, DiagnosticCode::missing_dependency, location, "image has neither URI nor bufferView");
                }
                std::string detected;
                budget.add_encoded_image(image.encoded_bytes.size(), location);
                const auto metadata = parse_image_metadata(image.encoded_bytes, image.mime_type, location, detected);
                if (!image.mime_type.empty() && image.mime_type != detected)
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, location + ".mimeType", "declared MIME type does not match image bytes", detected, image.mime_type);
                image.mime_type = detected;
                image.width = std::get<0>(metadata);
                image.height = std::get<1>(metadata);
                image.components = std::get<2>(metadata);
                const std::uint64_t decoded_bytes = checked_multiply(
                    checked_multiply(image.width, image.height, location + ".decoded"), 4U, location + ".decoded");
                budget.add_decoded_image(decoded_bytes, location + ".decoded");
                budget.observe_scratch(decoded_bytes, "image_decode_transient", location + ".decoded");
                try
                {
                    DecodedImage decoded = decode_image_rgba8(image.encoded_bytes, image.mime_type);
                    if (decoded.width != image.width || decoded.height != image.height || decoded.rgba8.size() != decoded_bytes)
                        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, location,
                             "full image decoder dimensions disagree with structural metadata");
                    image.row_stride = decoded.row_stride;
                    image.decoded_rgba8 = std::move(decoded.rgba8);
                }
                catch (const ImageDecodeError& error)
                {
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_image, location,
                         "supported image failed full pixel decoding", "decodable PNG or JPEG", error.what());
                }
                scene.images.push_back(std::move(image));
            }
        }

        if (const JsonValue::Array* texture_array = optional_array(root, "textures", "json"); texture_array != nullptr)
        {
            for (std::size_t index = 0; index < texture_array->size(); ++index)
            {
                const std::string location = "json.textures[" + std::to_string(index) + "]";
                const auto& object = require_object((*texture_array)[index], location);
                Texture texture;
                texture.name = optional_string(object, "name", location);
                const auto source_index = optional_uint(object, "source", location);
                if (!source_index.has_value() || *source_index >= scene.images.size())
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".source", "texture source is missing or out of range");
                texture.image = ImageId(static_cast<std::uint32_t>(*source_index));
                if (const auto sampler_index = optional_uint(object, "sampler", location); sampler_index.has_value())
                {
                    if (*sampler_index >= scene.samplers.size())
                        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".sampler", "texture sampler is out of range");
                    texture.sampler = SamplerId(static_cast<std::uint32_t>(*sampler_index));
                }
                scene.textures.push_back(std::move(texture));
            }
        }

        if (const JsonValue::Array* material_array = optional_array(root, "materials", "json"); material_array != nullptr)
        {
            std::vector<bool> used_as_srgb(scene.textures.size(), false), used_as_linear(scene.textures.size(), false);
            for (std::size_t index = 0; index < material_array->size(); ++index)
            {
                const std::string location = "json.materials[" + std::to_string(index) + "]";
                const auto& object = require_object((*material_array)[index], location);
                Material material;
                material.name = optional_string(object, "name", location);
                if (const JsonValue::Object* pbr = optional_object(object, "pbrMetallicRoughness", location); pbr != nullptr)
                {
                    if (const auto iterator = pbr->find("baseColorFactor"); iterator != pbr->end()) material.base_color_factor = read_vec4(iterator->second, location + ".pbrMetallicRoughness.baseColorFactor");
                    if (const auto iterator = pbr->find("metallicFactor"); iterator != pbr->end()) material.metallic_factor = number_to_float(iterator->second, location + ".pbrMetallicRoughness.metallicFactor");
                    if (const auto iterator = pbr->find("roughnessFactor"); iterator != pbr->end()) material.roughness_factor = number_to_float(iterator->second, location + ".pbrMetallicRoughness.roughnessFactor");
                    material.base_color_texture = parse_texture_reference(*pbr, "baseColorTexture", scene.textures.size(), diagnostics, location + ".pbrMetallicRoughness");
                    material.metallic_roughness_texture = parse_texture_reference(*pbr, "metallicRoughnessTexture", scene.textures.size(), diagnostics, location + ".pbrMetallicRoughness");
                }
                material.normal_texture = parse_texture_reference(object, "normalTexture", scene.textures.size(), diagnostics, location, &material.normal_scale);
                material.occlusion_texture = parse_texture_reference(object, "occlusionTexture", scene.textures.size(), diagnostics, location, &material.occlusion_strength);
                material.emissive_texture = parse_texture_reference(object, "emissiveTexture", scene.textures.size(), diagnostics, location);
                if (const auto iterator = object.find("emissiveFactor"); iterator != object.end()) material.emissive_factor = read_vec3(iterator->second, location + ".emissiveFactor");
                const std::string alpha = optional_string(object, "alphaMode", location);
                if (alpha.empty() || alpha == "OPAQUE") material.alpha_mode = AlphaMode::opaque;
                else if (alpha == "MASK") material.alpha_mode = AlphaMode::mask;
                else if (alpha == "BLEND") material.alpha_mode = AlphaMode::blend;
                else fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".alphaMode", "invalid alpha mode", "OPAQUE, MASK, or BLEND", alpha);
                if (const auto iterator = object.find("alphaCutoff"); iterator != object.end()) material.alpha_cutoff = number_to_float(iterator->second, location + ".alphaCutoff");
                material.double_sided = optional_bool(object, "doubleSided", false, location);
                if (!finite(material.base_color_factor) || !finite(material.emissive_factor) ||
                    !std::isfinite(material.metallic_factor) || !std::isfinite(material.roughness_factor) ||
                    !std::isfinite(material.normal_scale) || !std::isfinite(material.occlusion_strength) ||
                    !std::isfinite(material.alpha_cutoff))
                    fail(ImportStatus::invalid_source, DiagnosticCode::non_finite_data, location, "material contains non-finite values");
                if (!in_unit_interval(material.base_color_factor.x) || !in_unit_interval(material.base_color_factor.y) ||
                    !in_unit_interval(material.base_color_factor.z) || !in_unit_interval(material.base_color_factor.w) ||
                    !in_unit_interval(material.emissive_factor.x) || !in_unit_interval(material.emissive_factor.y) ||
                    !in_unit_interval(material.emissive_factor.z) || !in_unit_interval(material.metallic_factor) ||
                    !in_unit_interval(material.roughness_factor) || !in_unit_interval(material.occlusion_strength) ||
                    material.alpha_cutoff < 0.0F)
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location,
                         "material factors are outside the glTF 2.0 schema ranges");
                auto mark = [&](const std::optional<TextureReference>& reference, bool srgb)
                {
                    if (!reference.has_value()) return;
                    (srgb ? used_as_srgb : used_as_linear)[reference->texture.value()] = true;
                };
                mark(material.base_color_texture, true); mark(material.emissive_texture, true);
                mark(material.metallic_roughness_texture, false); mark(material.normal_texture, false); mark(material.occlusion_texture, false);
                scene.materials.push_back(std::move(material));
            }
            for (std::size_t index = 0; index < scene.textures.size(); ++index)
            {
                if (used_as_srgb[index] && used_as_linear[index])
                {
                    add_warning(diagnostics, DiagnosticCode::invalid_reference, DiagnosticDisposition::converted,
                                "json.textures[" + std::to_string(index) + "]",
                                "texture is referenced by both colour and data slots; canonical intent is linear and renderer may create per-use SRVs");
                    scene.textures[index].color_space = ColorSpaceIntent::linear;
                }
                else if (used_as_srgb[index]) scene.textures[index].color_space = ColorSpaceIntent::srgb;
            }
        }

        if (const JsonValue::Array* camera_array = optional_array(root, "cameras", "json"); camera_array != nullptr)
        {
            for (std::size_t index = 0; index < camera_array->size(); ++index)
            {
                const std::string location = "json.cameras[" + std::to_string(index) + "]";
                const auto& object = require_object((*camera_array)[index], location);
                Camera camera;
                camera.name = optional_string(object, "name", location);
                const std::string type = optional_string(object, "type", location);
                if (type == "perspective")
                {
                    camera.type = CameraType::perspective;
                    const JsonValue::Object* perspective = optional_object(object, "perspective", location);
                    if (perspective == nullptr) fail(ImportStatus::invalid_source, DiagnosticCode::invalid_camera, location, "perspective camera is missing its projection object");
                    camera.vertical_fov_radians = number_to_float(perspective->at("yfov"), location + ".perspective.yfov");
                    camera.near_plane = number_to_float(perspective->at("znear"), location + ".perspective.znear");
                    if (const auto iterator = perspective->find("zfar"); iterator != perspective->end()) camera.far_plane = number_to_float(iterator->second, location + ".perspective.zfar");
                    else camera.far_plane = std::numeric_limits<float>::infinity();
                    if (const auto iterator = perspective->find("aspectRatio"); iterator != perspective->end()) camera.aspect_ratio = number_to_float(iterator->second, location + ".perspective.aspectRatio");
                    if (!(camera.vertical_fov_radians > 0.0F && camera.vertical_fov_radians < 3.14159265F && camera.near_plane > 0.0F && (std::isinf(camera.far_plane) || camera.far_plane > camera.near_plane)))
                        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_camera, location, "perspective camera parameters are invalid");
                }
                else if (type == "orthographic")
                {
                    camera.type = CameraType::orthographic;
                    const JsonValue::Object* orthographic = optional_object(object, "orthographic", location);
                    if (orthographic == nullptr) fail(ImportStatus::invalid_source, DiagnosticCode::invalid_camera, location, "orthographic camera is missing its projection object");
                    camera.x_magnification = number_to_float(orthographic->at("xmag"), location + ".orthographic.xmag");
                    camera.y_magnification = number_to_float(orthographic->at("ymag"), location + ".orthographic.ymag");
                    camera.near_plane = number_to_float(orthographic->at("znear"), location + ".orthographic.znear");
                    camera.far_plane = number_to_float(orthographic->at("zfar"), location + ".orthographic.zfar");
                    if (!(camera.x_magnification > 0.0F && camera.y_magnification > 0.0F && camera.near_plane >= 0.0F && camera.far_plane > camera.near_plane))
                        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_camera, location, "orthographic camera parameters are invalid");
                }
                else fail(ImportStatus::invalid_source, DiagnosticCode::invalid_camera, location + ".type", "camera type must be perspective or orthographic");
                scene.cameras.push_back(std::move(camera));
            }
        }

        if (const JsonValue::Object* extensions = optional_object(root, "extensions", "json"); extensions != nullptr)
        {
            const auto lights_iterator = extensions->find("KHR_lights_punctual");
            if (lights_iterator != extensions->end())
            {
                const auto& light_extension = require_object(lights_iterator->second, "json.extensions.KHR_lights_punctual");
                if (const JsonValue::Array* lights = optional_array(light_extension, "lights", "json.extensions.KHR_lights_punctual"); lights != nullptr)
                {
                    for (std::size_t index = 0; index < lights->size(); ++index)
                    {
                        const std::string location = "json.extensions.KHR_lights_punctual.lights[" + std::to_string(index) + "]";
                        const auto& object = require_object((*lights)[index], location);
                        Light light;
                        light.name = optional_string(object, "name", location);
                        const std::string type = optional_string(object, "type", location);
                        if (type == "directional") light.type = LightType::directional;
                        else if (type == "point") light.type = LightType::point;
                        else if (type == "spot") light.type = LightType::spot;
                        else fail(ImportStatus::invalid_source, DiagnosticCode::invalid_light, location + ".type", "invalid punctual light type");
                        if (const auto iterator = object.find("color"); iterator != object.end()) light.color = read_vec3(iterator->second, location + ".color");
                        if (const auto iterator = object.find("intensity"); iterator != object.end()) light.intensity = number_to_float(iterator->second, location + ".intensity");
                        if (const auto iterator = object.find("range"); iterator != object.end()) light.range = number_to_float(iterator->second, location + ".range");
                        if (light.type == LightType::spot)
                        {
                            if (const JsonValue::Object* spot = optional_object(object, "spot", location); spot != nullptr)
                            {
                                if (const auto iterator = spot->find("innerConeAngle"); iterator != spot->end()) light.inner_cone_angle = number_to_float(iterator->second, location + ".spot.innerConeAngle");
                                if (const auto iterator = spot->find("outerConeAngle"); iterator != spot->end()) light.outer_cone_angle = number_to_float(iterator->second, location + ".spot.outerConeAngle");
                            }
                        }
                        if (!finite(light.color) || !std::isfinite(light.intensity) || light.intensity < 0.0F ||
                            (light.range.has_value() && (!std::isfinite(*light.range) || *light.range <= 0.0F)) ||
                            light.inner_cone_angle < 0.0F || light.outer_cone_angle <= 0.0F || light.inner_cone_angle > light.outer_cone_angle || light.outer_cone_angle > 1.57079633F)
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_light, location, "punctual light parameters are invalid");
                        scene.lights.push_back(std::move(light));
                    }
                }
            }
        }

        if (const JsonValue::Array* mesh_array = optional_array(root, "meshes", "json"); mesh_array != nullptr)
        {
            for (std::size_t mesh_index = 0; mesh_index < mesh_array->size(); ++mesh_index)
            {
                const std::string mesh_location = "json.meshes[" + std::to_string(mesh_index) + "]";
                const auto& mesh_object = require_object((*mesh_array)[mesh_index], mesh_location);
                Mesh mesh;
                mesh.name = optional_string(mesh_object, "name", mesh_location);
                const JsonValue::Array* primitives = optional_array(mesh_object, "primitives", mesh_location);
                if (primitives == nullptr || primitives->empty())
                    fail(ImportStatus::invalid_source, DiagnosticCode::empty_primitive, mesh_location + ".primitives", "mesh must contain at least one primitive");
                for (std::size_t primitive_index = 0; primitive_index < primitives->size(); ++primitive_index)
                {
                    const std::string location = mesh_location + ".primitives[" + std::to_string(primitive_index) + "]";
                    const auto& object = require_object((*primitives)[primitive_index], location);
                    const std::uint64_t mode = optional_uint(object, "mode", location).value_or(4);
                    if (mode != 4)
                        fail(ImportStatus::unsupported_feature, DiagnosticCode::unsupported_primitive_mode, location + ".mode", "Campaign B supports TRIANGLES only", "4", std::to_string(mode));
                    const JsonValue::Object* attributes = optional_object(object, "attributes", location);
                    if (attributes == nullptr)
                        fail(ImportStatus::invalid_source, DiagnosticCode::missing_position_attribute, location + ".attributes", "primitive attributes object is required");
                    const auto position_iterator = attributes->find("POSITION");
                    if (position_iterator == attributes->end())
                        fail(ImportStatus::invalid_source, DiagnosticCode::missing_position_attribute, location + ".attributes.POSITION", "POSITION attribute is required");
                    const std::size_t position_accessor = static_cast<std::size_t>(require_uint(position_iterator->second, location + ".attributes.POSITION"));
                    if (position_accessor >= accessors.size())
                        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".attributes.POSITION", "POSITION accessor index is out of range");
                    validate_mesh_attribute_accessor(accessors[position_accessor], "POSITION", mesh_quantization_enabled, location + ".attributes.POSITION");
                    budget.observe_scratch(checked_multiply(accessors[position_accessor].count, 3U * sizeof(float), location + ".POSITION"),
                                           "accessor_decode_scratch", location + ".POSITION");
                    const std::vector<float> positions = reader.read_floats(position_accessor, 3, location + ".POSITION");
                    const std::size_t vertex_count = positions.size() / 3U;
                    if (vertex_count == 0)
                        fail(ImportStatus::invalid_source, DiagnosticCode::empty_primitive, location, "primitive has no vertices");
                    Primitive primitive;
                    primitive.name = (mesh.name.empty() ? "mesh" + std::to_string(mesh_index) : mesh.name) + "/primitive" + std::to_string(primitive_index);
                    const std::uint64_t vertex_bytes = checked_multiply(vertex_count, sizeof(Vertex), location + ".vertices");
                    budget.add_geometry(vertex_bytes, location + ".vertices");
                    budget.observe_scratch(checked_multiply(accessors[position_accessor].count, 3U * sizeof(float), location + ".POSITION"),
                                           "position_decode_transient", location + ".POSITION");
                    primitive.vertices.resize(vertex_count);
                    Aabb decoded_position_bounds;
                    for (std::size_t index = 0; index < vertex_count; ++index)
                    {
                        primitive.vertices[index].position = {positions[index * 3], positions[index * 3 + 1], positions[index * 3 + 2]};
                        expand(decoded_position_bounds, primitive.vertices[index].position);
                    }
                    const AccessorData& position_metadata = accessors[position_accessor];
                    if (!position_metadata.declared_min.empty() && !position_metadata.declared_max.empty())
                    {
                        const std::array<float, 3> decoded_min{decoded_position_bounds.minimum.x, decoded_position_bounds.minimum.y, decoded_position_bounds.minimum.z};
                        const std::array<float, 3> decoded_max{decoded_position_bounds.maximum.x, decoded_position_bounds.maximum.y, decoded_position_bounds.maximum.z};
                        bool matches = true;
                        for (std::size_t component = 0; component < 3; ++component)
                        {
                            matches = matches && nearly_equal_bounds(position_metadata.declared_min[component], decoded_min[component]);
                            matches = matches && nearly_equal_bounds(position_metadata.declared_max[component], decoded_max[component]);
                        }
                        if (!matches)
                        {
                            std::ostringstream observed;
                            observed << "min=[" << position_metadata.declared_min[0] << ',' << position_metadata.declared_min[1] << ',' << position_metadata.declared_min[2]
                                     << "] max=[" << position_metadata.declared_max[0] << ',' << position_metadata.declared_max[1] << ',' << position_metadata.declared_max[2] << ']';
                            std::ostringstream expected;
                            expected << "min=[" << decoded_min[0] << ',' << decoded_min[1] << ',' << decoded_min[2]
                                     << "] max=[" << decoded_max[0] << ',' << decoded_max[1] << ',' << decoded_max[2] << ']';
                            add_warning(diagnostics, DiagnosticCode::bounds_recomputed, DiagnosticDisposition::repaired,
                                        location + ".attributes.POSITION",
                                        "declared POSITION bounds disagree with decoded data; canonical bounds use decoded positions",
                                        expected.str(), observed.str());
                        }
                    }

                    auto attribute_index = [&](std::string_view name) -> std::optional<std::size_t>
                    {
                        const auto iterator = attributes->find(name);
                        if (iterator == attributes->end()) return std::nullopt;
                        const std::size_t accessor_index = static_cast<std::size_t>(require_uint(iterator->second, location + ".attributes." + std::string(name)));
                        if (accessor_index >= accessors.size())
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference,
                                 location + ".attributes." + std::string(name), "attribute accessor index is out of range");
                        validate_mesh_attribute_accessor(accessors[accessor_index], name, mesh_quantization_enabled,
                                                         location + ".attributes." + std::string(name));
                        return accessor_index;
                    };
                    auto read_attribute = [&](std::string_view name, std::size_t components, auto assign, bool& flag)
                    {
                        const auto accessor_index = attribute_index(name);
                        if (!accessor_index.has_value()) return;
                        budget.observe_scratch(checked_multiply(accessors[*accessor_index].count,
                                                                  checked_multiply(components, sizeof(float), location), location),
                                               "accessor_decode_scratch", location + "." + std::string(name));
                        const std::vector<float> values = reader.read_floats(*accessor_index, components, location + "." + std::string(name));
                        if (values.size() / components != vertex_count)
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_accessor, location + "." + std::string(name), "attribute count does not match POSITION count");
                        for (std::size_t index = 0; index < vertex_count; ++index) assign(primitive.vertices[index], values.data() + index * components);
                        flag = true;
                    };
                    read_attribute("NORMAL", 3, [&](Vertex& vertex, const float* value)
                    {
                        const Vec3 source_value{value[0], value[1], value[2]};
                        const float magnitude = length(source_value);
                        if (!(magnitude > 1.0e-8F) || !std::isfinite(magnitude))
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_unit_vector,
                                 location + ".NORMAL", "normal must have finite non-zero length");
                        const float deviation = std::abs(magnitude - 1.0F);
                        if (deviation > 1.0e-2F)
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_unit_vector,
                                 location + ".NORMAL", "normal length is too far from unit length to repair",
                                 "length within 0.01 of 1", std::to_string(magnitude));
                        vertex.normal = source_value / magnitude;
                        if (deviation > 1.0e-5F)
                            add_warning(diagnostics, DiagnosticCode::attribute_normalized, DiagnosticDisposition::repaired,
                                        location + ".NORMAL", "slightly non-unit normal was normalized",
                                        "unit length", std::to_string(magnitude));
                    }, primitive.has_normals);
                    read_attribute("TANGENT", 4, [&](Vertex& vertex, const float* value)
                    {
                        const Vec3 source_value{value[0], value[1], value[2]};
                        const float magnitude = length(source_value);
                        if (!(magnitude > 1.0e-8F) || !std::isfinite(magnitude))
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_unit_vector,
                                 location + ".TANGENT", "tangent XYZ must have finite non-zero length");
                        const float deviation = std::abs(magnitude - 1.0F);
                        if (deviation > 1.0e-2F)
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_unit_vector,
                                 location + ".TANGENT", "tangent length is too far from unit length to repair",
                                 "length within 0.01 of 1", std::to_string(magnitude));
                        const Vec3 tangent = source_value / magnitude;
                        vertex.tangent = {tangent.x, tangent.y, tangent.z, value[3]};
                        if (deviation > 1.0e-5F)
                            add_warning(diagnostics, DiagnosticCode::attribute_normalized, DiagnosticDisposition::repaired,
                                        location + ".TANGENT", "slightly non-unit tangent was normalized while preserving handedness",
                                        "unit XYZ length", std::to_string(magnitude));
                    }, primitive.has_tangents);
                    read_attribute("TEXCOORD_0", 2, [](Vertex& vertex, const float* value) { vertex.texcoord0 = {value[0], value[1]}; }, primitive.has_texcoord0);
                    read_attribute("TEXCOORD_1", 2, [](Vertex& vertex, const float* value) { vertex.texcoord1 = {value[0], value[1]}; }, primitive.has_texcoord1);
                    if (const auto color_accessor = attribute_index("COLOR_0"); color_accessor.has_value())
                    {
                        const std::size_t components = component_count(accessors[*color_accessor].type, location + ".COLOR_0");
                        const std::vector<float> values = reader.read_floats(*color_accessor, components, location + ".COLOR_0");
                        if (values.size() / components != vertex_count)
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_accessor, location + ".COLOR_0", "attribute count does not match POSITION count");
                        for (std::size_t index = 0; index < vertex_count; ++index)
                        {
                            const float* value = values.data() + index * components;
                            primitive.vertices[index].color = {value[0], value[1], value[2], components == 4 ? value[3] : 1.0F};
                        }
                        primitive.has_colors = true;
                    }
                    if (primitive.has_tangents)
                    {
                        for (std::size_t index = 0; index < primitive.vertices.size(); ++index)
                        {
                            const float handedness = primitive.vertices[index].tangent.w;
                            if (!nearly_equal_bounds(std::abs(handedness), 1.0F))
                                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_accessor,
                                     location + ".TANGENT.element[" + std::to_string(index) + "].w",
                                     "tangent handedness must be -1 or +1", "-1 or +1", std::to_string(handedness));
                            primitive.vertices[index].tangent.w = handedness < 0.0F ? -1.0F : 1.0F;
                        }
                    }
                    if (!primitive.has_normals)
                        add_warning(diagnostics, DiagnosticCode::missing_optional_attribute, DiagnosticDisposition::defaulted, location + ".attributes.NORMAL", "missing normals were defaulted to +Z for diagnostic rendering");
                    if (!primitive.has_tangents)
                        add_warning(diagnostics, DiagnosticCode::missing_optional_attribute, DiagnosticDisposition::defaulted, location + ".attributes.TANGENT", "missing tangents were defaulted to +X with positive handedness");

                    if (const auto indices = optional_uint(object, "indices", location); indices.has_value())
                    {
                        if (*indices >= accessors.size())
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".indices", "index accessor is out of range");
                        budget.add_geometry(checked_multiply(accessors[static_cast<std::size_t>(*indices)].count,
                                                             sizeof(std::uint32_t), location + ".indices"), location + ".indices");
                        primitive.indices = reader.read_indices(static_cast<std::size_t>(*indices), location + ".indices");
                    }
                    else
                    {
                        budget.add_geometry(checked_multiply(vertex_count, sizeof(std::uint32_t), location + ".indices"), location + ".indices");
                        primitive.indices.resize(vertex_count);
                        for (std::size_t index = 0; index < vertex_count; ++index) primitive.indices[index] = static_cast<std::uint32_t>(index);
                        add_warning(diagnostics, DiagnosticCode::invalid_accessor, DiagnosticDisposition::converted, location + ".indices", "non-indexed primitive was converted to explicit sequential indices");
                    }
                    if (primitive.indices.size() % 3U != 0)
                        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_accessor, location + ".indices", "TRIANGLES index count must be divisible by three");
                    std::size_t degenerate_count = 0;
                    for (std::size_t index = 0; index < primitive.indices.size(); ++index)
                    {
                        if (primitive.indices[index] >= vertex_count)
                            fail(ImportStatus::invalid_source, DiagnosticCode::index_out_of_range,
                                 location + ".indices[" + std::to_string(index) + "]", "index exceeds vertex count",
                                 "< " + std::to_string(vertex_count), std::to_string(primitive.indices[index]));
                    }
                    for (std::size_t index = 0; index < primitive.indices.size(); index += 3)
                    {
                        const Vec3 a = primitive.vertices[primitive.indices[index]].position;
                        const Vec3 b = primitive.vertices[primitive.indices[index + 1]].position;
                        const Vec3 c = primitive.vertices[primitive.indices[index + 2]].position;
                        if (length(cross(b - a, c - a)) <= 1.0e-12F) ++degenerate_count;
                    }
                    if (degenerate_count != 0)
                        add_warning(diagnostics, DiagnosticCode::degenerate_triangle, DiagnosticDisposition::observed, location,
                                    "primitive contains degenerate triangles", "0", std::to_string(degenerate_count));
                    if (const auto material = optional_uint(object, "material", location); material.has_value())
                    {
                        if (*material >= scene.materials.size())
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".material", "material index is out of range");
                        primitive.material = MaterialId(static_cast<std::uint32_t>(*material));
                    }
                    // An invalid MaterialId means the glTF-specified default material. Source material indices remain stable.
                    const Material& resolved_material = primitive.material.valid() ? scene.materials[primitive.material.value()] : scene.default_material;
                    const auto validate_texcoord = [&](const std::optional<TextureReference>& reference, std::string_view slot)
                    {
                        if (!reference.has_value()) return;
                        const bool available = reference->texcoord_set == 0U ? primitive.has_texcoord0 : primitive.has_texcoord1;
                        if (!available)
                            fail(ImportStatus::invalid_source, DiagnosticCode::missing_texture_coordinate,
                                 location + ".material." + std::string(slot),
                                 "material texture references a TEXCOORD set absent from the primitive",
                                 "TEXCOORD_" + std::to_string(reference->texcoord_set), "missing");
                    };
                    validate_texcoord(resolved_material.base_color_texture, "baseColorTexture");
                    validate_texcoord(resolved_material.metallic_roughness_texture, "metallicRoughnessTexture");
                    validate_texcoord(resolved_material.normal_texture, "normalTexture");
                    validate_texcoord(resolved_material.occlusion_texture, "occlusionTexture");
                    validate_texcoord(resolved_material.emissive_texture, "emissiveTexture");
                    scene.primitives.push_back(std::move(primitive));
                    mesh.primitives.push_back(PrimitiveId(static_cast<std::uint32_t>(scene.primitives.size() - 1)));
                }
                scene.meshes.push_back(std::move(mesh));
            }
        }

        if (const JsonValue::Array* node_array = optional_array(root, "nodes", "json"); node_array != nullptr)
        {
            scene.nodes.resize(node_array->size());
            for (std::size_t index = 0; index < node_array->size(); ++index)
            {
                const std::string location = "json.nodes[" + std::to_string(index) + "]";
                const auto& object = require_object((*node_array)[index], location);
                Node& node = scene.nodes[index];
                node.name = optional_string(object, "name", location);
                const bool has_matrix = object.contains("matrix");
                const bool has_trs = object.contains("translation") || object.contains("rotation") || object.contains("scale");
                if (has_matrix && has_trs)
                    fail(ImportStatus::invalid_source, DiagnosticCode::invalid_node_graph, location, "node may not define both matrix and TRS");
                if (has_matrix)
                {
                    const auto& matrix = require_array(object.at("matrix"), location + ".matrix");
                    if (matrix.size() != 16) fail(ImportStatus::invalid_source, DiagnosticCode::invalid_node_graph, location + ".matrix", "node matrix must have 16 components");
                    node.transform_source = TransformSource::matrix;
                    for (std::size_t component = 0; component < 16; ++component) node.local_transform.values[component] = number_to_float(matrix[component], location + ".matrix");
                }
                else
                {
                    node.transform_source = has_trs ? TransformSource::trs : TransformSource::identity;
                    if (const auto iterator = object.find("translation"); iterator != object.end()) node.translation = read_vec3(iterator->second, location + ".translation");
                    if (const auto iterator = object.find("rotation"); iterator != object.end())
                    {
                        const Vec4 rotation = read_vec4(iterator->second, location + ".rotation");
                        const float magnitude = std::sqrt(rotation.x * rotation.x + rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w);
                        if (!(magnitude > 1.0e-8F) || !std::isfinite(magnitude))
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_unit_vector,
                                 location + ".rotation", "rotation quaternion must have finite non-zero length");
                        const float deviation = std::abs(magnitude - 1.0F);
                        if (deviation > 1.0e-2F)
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_unit_vector,
                                 location + ".rotation", "rotation quaternion is too far from unit length to repair",
                                 "length within 0.01 of 1", std::to_string(magnitude));
                        node.rotation = {rotation.x / magnitude, rotation.y / magnitude, rotation.z / magnitude, rotation.w / magnitude};
                        if (deviation > 1.0e-5F)
                            add_warning(diagnostics, DiagnosticCode::rotation_normalized, DiagnosticDisposition::repaired,
                                        location + ".rotation", "slightly non-unit rotation quaternion was normalized",
                                        "unit length", std::to_string(magnitude));
                    }
                    if (const auto iterator = object.find("scale"); iterator != object.end()) node.scale = read_vec3(iterator->second, location + ".scale");
                    node.local_transform = compose_trs(node.translation, node.rotation, node.scale);
                }
                if (!finite(node.local_transform)) fail(ImportStatus::invalid_source, DiagnosticCode::non_finite_data, location, "node transform contains non-finite values");
                if (std::abs(determinant3x3(node.local_transform)) <= 1.0e-20F)
                    add_warning(diagnostics, DiagnosticCode::singular_transform, DiagnosticDisposition::observed, location, "node transform is singular");
                if (const auto mesh = optional_uint(object, "mesh", location); mesh.has_value())
                {
                    if (*mesh >= scene.meshes.size()) fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".mesh", "node mesh index is out of range");
                    node.mesh = MeshId(static_cast<std::uint32_t>(*mesh));
                }
                if (const auto camera = optional_uint(object, "camera", location); camera.has_value())
                {
                    if (*camera >= scene.cameras.size()) fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".camera", "node camera index is out of range");
                    node.camera = CameraId(static_cast<std::uint32_t>(*camera));
                }
                if (const JsonValue::Array* children = optional_array(object, "children", location); children != nullptr)
                {
                    for (std::size_t child_index = 0; child_index < children->size(); ++child_index)
                    {
                        const std::uint64_t child = require_uint((*children)[child_index], location + ".children[" + std::to_string(child_index) + "]");
                        if (child >= scene.nodes.size()) fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".children", "child node index is out of range");
                        node.children.emplace_back(static_cast<std::uint32_t>(child));
                    }
                }
                if (const JsonValue::Object* extensions = optional_object(object, "extensions", location); extensions != nullptr)
                {
                    const auto light_iterator = extensions->find("KHR_lights_punctual");
                    if (light_iterator != extensions->end())
                    {
                        const auto& light_object = require_object(light_iterator->second, location + ".extensions.KHR_lights_punctual");
                        const auto light = optional_uint(light_object, "light", location);
                        if (!light.has_value() || *light >= scene.lights.size()) fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".extensions.KHR_lights_punctual.light", "light index is missing or out of range");
                        node.light = LightId(static_cast<std::uint32_t>(*light));
                    }
                }
            }
            for (std::size_t parent_index = 0; parent_index < scene.nodes.size(); ++parent_index)
            {
                for (const NodeId child : scene.nodes[parent_index].children)
                {
                    Node& child_node = scene.nodes[child.value()];
                    if (child_node.parent.valid())
                        fail(ImportStatus::invalid_source, DiagnosticCode::invalid_node_graph, "json.nodes[" + std::to_string(child.value()) + "]", "node has more than one parent");
                    child_node.parent = NodeId(static_cast<std::uint32_t>(parent_index));
                }
            }
            std::string graph_error;
            if (!propagate_world_transforms(scene, &graph_error))
                fail(ImportStatus::invalid_source, DiagnosticCode::invalid_node_graph, "json.nodes", graph_error);
        }

        if (const JsonValue::Array* scene_array = optional_array(root, "scenes", "json"); scene_array != nullptr)
        {
            for (std::size_t index = 0; index < scene_array->size(); ++index)
            {
                const std::string location = "json.scenes[" + std::to_string(index) + "]";
                const auto& object = require_object((*scene_array)[index], location);
                SceneDefinition definition;
                definition.name = optional_string(object, "name", location);
                if (const JsonValue::Array* nodes = optional_array(object, "nodes", location); nodes != nullptr)
                {
                    for (std::size_t node_index = 0; node_index < nodes->size(); ++node_index)
                    {
                        const std::uint64_t node = require_uint((*nodes)[node_index], location + ".nodes[" + std::to_string(node_index) + "]");
                        if (node >= scene.nodes.size()) fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, location + ".nodes", "scene root index is out of range");
                        if (scene.nodes[static_cast<std::size_t>(node)].parent.valid())
                            fail(ImportStatus::invalid_source, DiagnosticCode::invalid_node_graph, location + ".nodes", "scene root node has a parent");
                        definition.roots.emplace_back(static_cast<std::uint32_t>(node));
                    }
                }
                scene.scenes.push_back(std::move(definition));
            }
        }
        if (scene.scenes.empty())
        {
            SceneDefinition definition;
            definition.name = "Implicit roots";
            for (std::size_t index = 0; index < scene.nodes.size(); ++index)
                if (!scene.nodes[index].parent.valid()) definition.roots.emplace_back(static_cast<std::uint32_t>(index));
            scene.scenes.push_back(std::move(definition));
            add_warning(diagnostics, DiagnosticCode::implicit_scene_created, DiagnosticDisposition::defaulted, "json.scenes", "source has no scenes; an implicit scene containing all root nodes was created");
        }

        std::size_t selected_scene = 0;
        if (settings.scene_selector.has_value())
        {
            const std::string& selector = *settings.scene_selector;
            std::uint64_t numeric = 0;
            const auto [end, error] = std::from_chars(selector.data(), selector.data() + selector.size(), numeric);
            if (error == std::errc{} && end == selector.data() + selector.size())
            {
                if (numeric >= scene.scenes.size()) fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, "settings.scene", "requested scene index is out of range");
                selected_scene = static_cast<std::size_t>(numeric);
            }
            else
            {
                const auto iterator = std::find_if(scene.scenes.begin(), scene.scenes.end(), [&](const SceneDefinition& definition) { return definition.name == selector; });
                if (iterator == scene.scenes.end()) fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, "settings.scene", "requested scene name was not found", {}, selector);
                selected_scene = static_cast<std::size_t>(std::distance(scene.scenes.begin(), iterator));
            }
        }
        else if (const auto default_scene = optional_uint(root, "scene", "json"); default_scene.has_value())
        {
            if (*default_scene >= scene.scenes.size()) fail(ImportStatus::invalid_source, DiagnosticCode::invalid_reference, "json.scene", "default scene index is out of range");
            selected_scene = static_cast<std::size_t>(*default_scene);
            add_information(diagnostics, DiagnosticCode::default_scene_selected, DiagnosticDisposition::observed, "json.scene", "source default scene was selected");
        }
        scene.selected_scene = SceneDefinitionId(static_cast<std::uint32_t>(selected_scene));

        recompute_bounds(scene);
        add_information(diagnostics, DiagnosticCode::bounds_recomputed, DiagnosticDisposition::repaired, "canonical.bounds", "all primitive, mesh, node, and scene bounds were recomputed from decoded positions");
        std::sort(scene.source.dependencies.begin(), scene.source.dependencies.end(), [](const auto& left, const auto& right)
        {
            return std::tie(left.normalized_relative_path, left.sha256) < std::tie(right.normalized_relative_path, right.sha256);
        });
        scene.source.dependencies.erase(std::unique(scene.source.dependencies.begin(), scene.source.dependencies.end(), [](const auto& left, const auto& right)
        {
            return left.normalized_relative_path == right.normalized_relative_path && left.sha256 == right.sha256;
        }), scene.source.dependencies.end());
        scene.source.deterministic_asset_key = compose_asset_key(scene, settings_description);
        final_status = status_from_diagnostics(diagnostics);
    }
    catch (const ImportFailure& error)
    {
        diagnostics.push_back(error.diagnostic());
        final_status = error.status();
    }
    catch (const std::bad_alloc&)
    {
        diagnostics.push_back({DiagnosticSeverity::error, DiagnosticCode::source_too_large, DiagnosticDisposition::rejected,
                               "import", "allocation failed while importing asset", {}, {}});
        final_status = ImportStatus::resource_limit;
    }
    catch (const std::exception& error)
    {
        diagnostics.push_back({DiagnosticSeverity::error, DiagnosticCode::internal_invariant, DiagnosticDisposition::rejected,
                               "import", "unexpected importer exception", {}, error.what()});
        final_status = ImportStatus::internal_error;
    }

    ImportReport report = make_import_report(scene, final_status, std::move(diagnostics), settings_description);
    for (const std::string& extension : scene.source.extensions_used)
    {
        if (extension == "KHR_lights_punctual" || extension == "KHR_mesh_quantization") continue;
        if (std::find(scene.source.extensions_required.begin(), scene.source.extensions_required.end(), extension) != scene.source.extensions_required.end())
            report.extensions_rejected.push_back(extension);
        else report.extensions_ignored.push_back(extension);
    }
    return {std::move(scene), std::move(report)};
}
}

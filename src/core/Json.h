#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace daedalus
{
class JsonError final : public std::runtime_error
{
public:
    JsonError(std::string message, std::size_t offset);
    [[nodiscard]] std::size_t offset() const noexcept;

private:
    std::size_t offset_ = 0;
};

class JsonValue final
{
public:
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    JsonValue() noexcept;
    JsonValue(std::nullptr_t) noexcept;
    JsonValue(bool value) noexcept;
    JsonValue(double value) noexcept;
    JsonValue(std::int64_t value) noexcept;
    JsonValue(std::uint64_t value) noexcept;
    JsonValue(std::string value);
    JsonValue(const char* value);
    JsonValue(Array value);
    JsonValue(Object value);

    [[nodiscard]] bool is_null() const noexcept;
    [[nodiscard]] bool is_bool() const noexcept;
    [[nodiscard]] bool is_number() const noexcept;
    [[nodiscard]] bool is_string() const noexcept;
    [[nodiscard]] bool is_array() const noexcept;
    [[nodiscard]] bool is_object() const noexcept;

    [[nodiscard]] bool as_bool() const;
    [[nodiscard]] double as_number() const;
    [[nodiscard]] const std::string& as_string() const;
    [[nodiscard]] const Array& as_array() const;
    [[nodiscard]] const Object& as_object() const;
    [[nodiscard]] Array& as_array();
    [[nodiscard]] Object& as_object();

    [[nodiscard]] const JsonValue* find(std::string_view key) const noexcept;

private:
    Storage storage_;
};

[[nodiscard]] JsonValue parse_json(std::string_view text, std::size_t maximum_depth = 128);
[[nodiscard]] std::string serialize_json(const JsonValue& value, bool pretty = true);
}

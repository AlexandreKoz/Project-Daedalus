#include "core/Json.h"

#include <charconv>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <utility>

namespace daedalus
{
JsonError::JsonError(std::string message, std::size_t offset)
    : std::runtime_error(std::move(message)), offset_(offset)
{
}

std::size_t JsonError::offset() const noexcept
{
    return offset_;
}

JsonValue::JsonValue() noexcept : storage_(nullptr) {}
JsonValue::JsonValue(std::nullptr_t) noexcept : storage_(nullptr) {}
JsonValue::JsonValue(bool value) noexcept : storage_(value) {}
JsonValue::JsonValue(double value) noexcept : storage_(value) {}
JsonValue::JsonValue(std::int64_t value) noexcept : storage_(static_cast<double>(value)) {}
JsonValue::JsonValue(std::uint64_t value) noexcept : storage_(static_cast<double>(value)) {}
JsonValue::JsonValue(std::string value) : storage_(std::move(value)) {}
JsonValue::JsonValue(const char* value) : storage_(std::string(value)) {}
JsonValue::JsonValue(Array value) : storage_(std::move(value)) {}
JsonValue::JsonValue(Object value) : storage_(std::move(value)) {}

bool JsonValue::is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(storage_); }
bool JsonValue::is_bool() const noexcept { return std::holds_alternative<bool>(storage_); }
bool JsonValue::is_number() const noexcept { return std::holds_alternative<double>(storage_); }
bool JsonValue::is_string() const noexcept { return std::holds_alternative<std::string>(storage_); }
bool JsonValue::is_array() const noexcept { return std::holds_alternative<Array>(storage_); }
bool JsonValue::is_object() const noexcept { return std::holds_alternative<Object>(storage_); }

bool JsonValue::as_bool() const { return std::get<bool>(storage_); }
double JsonValue::as_number() const { return std::get<double>(storage_); }
const std::string& JsonValue::as_string() const { return std::get<std::string>(storage_); }
const JsonValue::Array& JsonValue::as_array() const { return std::get<Array>(storage_); }
const JsonValue::Object& JsonValue::as_object() const { return std::get<Object>(storage_); }
JsonValue::Array& JsonValue::as_array() { return std::get<Array>(storage_); }
JsonValue::Object& JsonValue::as_object() { return std::get<Object>(storage_); }

const JsonValue* JsonValue::find(std::string_view key) const noexcept
{
    if (!is_object())
    {
        return nullptr;
    }
    const auto iterator = as_object().find(key);
    return iterator == as_object().end() ? nullptr : &iterator->second;
}

namespace
{
class Parser final
{
public:
    Parser(std::string_view text, std::size_t maximum_depth) : text_(text), maximum_depth_(maximum_depth) {}

    [[nodiscard]] JsonValue parse()
    {
        skip_whitespace();
        JsonValue value = parse_value(0);
        skip_whitespace();
        if (position_ != text_.size())
        {
            fail("unexpected trailing JSON data");
        }
        return value;
    }

private:
    [[noreturn]] void fail(std::string message) const
    {
        throw JsonError(std::move(message), position_);
    }

    void skip_whitespace()
    {
        while (position_ < text_.size())
        {
            const char character = text_[position_];
            if (character != ' ' && character != '\t' && character != '\r' && character != '\n')
            {
                break;
            }
            ++position_;
        }
    }

    [[nodiscard]] JsonValue parse_value(std::size_t depth)
    {
        if (depth > maximum_depth_)
        {
            fail("JSON nesting depth exceeds configured limit");
        }
        if (position_ >= text_.size())
        {
            fail("unexpected end of JSON input");
        }

        switch (text_[position_])
        {
        case 'n': return parse_literal("null", JsonValue(nullptr));
        case 't': return parse_literal("true", JsonValue(true));
        case 'f': return parse_literal("false", JsonValue(false));
        case '"': return JsonValue(parse_string());
        case '[': return parse_array(depth + 1);
        case '{': return parse_object(depth + 1);
        default:
            if (text_[position_] == '-' || (text_[position_] >= '0' && text_[position_] <= '9'))
            {
                return JsonValue(parse_number());
            }
            fail("unexpected token in JSON input");
        }
    }

    [[nodiscard]] JsonValue parse_literal(std::string_view literal, JsonValue value)
    {
        if (text_.substr(position_, literal.size()) != literal)
        {
            fail("invalid JSON literal");
        }
        position_ += literal.size();
        return value;
    }

    static void append_utf8(std::string& output, std::uint32_t codepoint)
    {
        if (codepoint <= 0x7FU)
        {
            output.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint <= 0x7FFU)
        {
            output.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        }
        else if (codepoint <= 0xFFFFU)
        {
            output.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        }
        else
        {
            output.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
            output.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
        }
    }

    [[nodiscard]] std::uint32_t parse_hex_quad()
    {
        if (position_ + 4 > text_.size())
        {
            fail("truncated JSON unicode escape");
        }
        std::uint32_t value = 0;
        for (int index = 0; index < 4; ++index)
        {
            const char character = text_[position_++];
            value <<= 4U;
            if (character >= '0' && character <= '9') value |= static_cast<std::uint32_t>(character - '0');
            else if (character >= 'a' && character <= 'f') value |= static_cast<std::uint32_t>(character - 'a' + 10);
            else if (character >= 'A' && character <= 'F') value |= static_cast<std::uint32_t>(character - 'A' + 10);
            else fail("invalid hexadecimal digit in JSON unicode escape");
        }
        return value;
    }

    [[nodiscard]] std::string parse_string()
    {
        if (text_[position_++] != '"')
        {
            fail("internal JSON string parser error");
        }
        std::string output;
        while (position_ < text_.size())
        {
            const unsigned char character = static_cast<unsigned char>(text_[position_++]);
            if (character == '"')
            {
                return output;
            }
            if (character < 0x20U)
            {
                fail("unescaped control character in JSON string");
            }
            if (character != '\\')
            {
                output.push_back(static_cast<char>(character));
                continue;
            }
            if (position_ >= text_.size())
            {
                fail("truncated JSON escape sequence");
            }
            const char escape = text_[position_++];
            switch (escape)
            {
            case '"': output.push_back('"'); break;
            case '\\': output.push_back('\\'); break;
            case '/': output.push_back('/'); break;
            case 'b': output.push_back('\b'); break;
            case 'f': output.push_back('\f'); break;
            case 'n': output.push_back('\n'); break;
            case 'r': output.push_back('\r'); break;
            case 't': output.push_back('\t'); break;
            case 'u':
            {
                std::uint32_t codepoint = parse_hex_quad();
                if (codepoint >= 0xD800U && codepoint <= 0xDBFFU)
                {
                    if (position_ + 2 > text_.size() || text_[position_] != '\\' || text_[position_ + 1] != 'u')
                    {
                        fail("JSON high surrogate is not followed by a low surrogate");
                    }
                    position_ += 2;
                    const std::uint32_t low = parse_hex_quad();
                    if (low < 0xDC00U || low > 0xDFFFU)
                    {
                        fail("invalid JSON low surrogate");
                    }
                    codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
                }
                else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU)
                {
                    fail("unexpected JSON low surrogate");
                }
                append_utf8(output, codepoint);
                break;
            }
            default: fail("invalid JSON escape sequence");
            }
        }
        fail("unterminated JSON string");
    }

    [[nodiscard]] double parse_number()
    {
        const std::size_t start = position_;
        if (text_[position_] == '-') ++position_;
        if (position_ >= text_.size()) fail("truncated JSON number");
        if (text_[position_] == '0')
        {
            ++position_;
        }
        else
        {
            if (text_[position_] < '1' || text_[position_] > '9') fail("invalid JSON number");
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }
        if (position_ < text_.size() && text_[position_] == '.')
        {
            ++position_;
            if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') fail("invalid JSON fraction");
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E'))
        {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
            if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') fail("invalid JSON exponent");
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
        }

        double value = 0.0;
        const std::string token(text_.substr(start, position_ - start));
        std::istringstream stream(token);
        stream.imbue(std::locale::classic());
        stream >> value;
        if (!stream || !stream.eof() || !std::isfinite(value))
        {
            fail("JSON number is not finite or representable");
        }
        return value;
    }

    [[nodiscard]] JsonValue parse_array(std::size_t depth)
    {
        ++position_;
        JsonValue::Array array;
        skip_whitespace();
        if (position_ < text_.size() && text_[position_] == ']')
        {
            ++position_;
            return JsonValue(std::move(array));
        }
        for (;;)
        {
            skip_whitespace();
            array.push_back(parse_value(depth));
            skip_whitespace();
            if (position_ >= text_.size()) fail("unterminated JSON array");
            const char separator = text_[position_++];
            if (separator == ']') break;
            if (separator != ',') fail("expected comma or closing bracket in JSON array");
        }
        return JsonValue(std::move(array));
    }

    [[nodiscard]] JsonValue parse_object(std::size_t depth)
    {
        ++position_;
        JsonValue::Object object;
        skip_whitespace();
        if (position_ < text_.size() && text_[position_] == '}')
        {
            ++position_;
            return JsonValue(std::move(object));
        }
        for (;;)
        {
            skip_whitespace();
            if (position_ >= text_.size() || text_[position_] != '"') fail("expected JSON object key");
            std::string key = parse_string();
            skip_whitespace();
            if (position_ >= text_.size() || text_[position_++] != ':') fail("expected colon after JSON object key");
            skip_whitespace();
            JsonValue value = parse_value(depth);
            if (!object.emplace(std::move(key), std::move(value)).second)
            {
                fail("duplicate JSON object key");
            }
            skip_whitespace();
            if (position_ >= text_.size()) fail("unterminated JSON object");
            const char separator = text_[position_++];
            if (separator == '}') break;
            if (separator != ',') fail("expected comma or closing brace in JSON object");
        }
        return JsonValue(std::move(object));
    }

    std::string_view text_;
    std::size_t position_ = 0;
    std::size_t maximum_depth_ = 128;
};

void append_indent(std::string& output, int level)
{
    output.append(static_cast<std::size_t>(level) * 2U, ' ');
}

void append_escaped(std::string& output, std::string_view value)
{
    output.push_back('"');
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20U)
            {
                output += "\\u00";
                output.push_back(hex[character >> 4U]);
                output.push_back(hex[character & 0x0FU]);
            }
            else
            {
                output.push_back(static_cast<char>(character));
            }
        }
    }
    output.push_back('"');
}

void serialize_value(std::string& output, const JsonValue& value, bool pretty, int level)
{
    if (value.is_null()) { output += "null"; return; }
    if (value.is_bool()) { output += value.as_bool() ? "true" : "false"; return; }
    if (value.is_number())
    {
        std::ostringstream stream;
        stream.imbue(std::locale::classic());
        stream << std::setprecision(17) << value.as_number();
        output += stream.str();
        return;
    }
    if (value.is_string()) { append_escaped(output, value.as_string()); return; }
    if (value.is_array())
    {
        output.push_back('[');
        const auto& array = value.as_array();
        for (std::size_t index = 0; index < array.size(); ++index)
        {
            if (index != 0) output.push_back(',');
            if (pretty) { output.push_back('\n'); append_indent(output, level + 1); }
            serialize_value(output, array[index], pretty, level + 1);
        }
        if (pretty && !array.empty()) { output.push_back('\n'); append_indent(output, level); }
        output.push_back(']');
        return;
    }

    output.push_back('{');
    const auto& object = value.as_object();
    std::size_t index = 0;
    for (const auto& [key, item] : object)
    {
        if (index++ != 0) output.push_back(',');
        if (pretty) { output.push_back('\n'); append_indent(output, level + 1); }
        append_escaped(output, key);
        output += pretty ? ": " : ":";
        serialize_value(output, item, pretty, level + 1);
    }
    if (pretty && !object.empty()) { output.push_back('\n'); append_indent(output, level); }
    output.push_back('}');
}
}

JsonValue parse_json(std::string_view text, std::size_t maximum_depth)
{
    return Parser(text, maximum_depth).parse();
}

std::string serialize_json(const JsonValue& value, bool pretty)
{
    std::string output;
    serialize_value(output, value, pretty, 0);
    if (pretty) output.push_back('\n');
    return output;
}
}

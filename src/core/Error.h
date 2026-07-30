#pragma once

#include <cstdint>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>

namespace daedalus
{
using ResultCode = std::int32_t;

[[nodiscard]] bool result_failed(ResultCode result) noexcept;
[[nodiscard]] std::string format_result_code(ResultCode result);
[[nodiscard]] std::string format_failure_message(
    ResultCode result,
    std::string_view operation,
    const std::source_location& location = std::source_location::current());

class ResultError final : public std::runtime_error
{
public:
    ResultError(
        ResultCode result,
        std::string operation,
        const std::source_location& location = std::source_location::current());

    [[nodiscard]] ResultCode result() const noexcept;
    [[nodiscard]] const std::string& operation() const noexcept;

private:
    ResultCode result_;
    std::string operation_;
};

void throw_if_failed(
    ResultCode result,
    std::string_view operation,
    const std::source_location& location = std::source_location::current());
}

#define DAEDALUS_THROW_IF_FAILED(expression)                                                                    \
    ::daedalus::throw_if_failed(static_cast<::daedalus::ResultCode>(expression), #expression, std::source_location::current())

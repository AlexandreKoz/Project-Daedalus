#include "core/CommandLine.h"
#include "core/Error.h"
#include "graphics/AdapterPolicy.h"

#include <array>
#include <cstdint>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
class TestFailure final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void require(bool condition, std::string_view message)
{
    if (!condition)
    {
        throw TestFailure(std::string(message));
    }
}

template <typename Exception, typename Callable>
void require_throws(Callable&& callable, std::string_view message)
{
    try
    {
        std::invoke(std::forward<Callable>(callable));
    }
    catch (const Exception&)
    {
        return;
    }
    catch (...)
    {
        throw TestFailure(std::string(message) + " (wrong exception type)");
    }
    throw TestFailure(std::string(message) + " (no exception)");
}

void test_command_line_defaults()
{
    const daedalus::CommandLineOptions options = daedalus::parse_command_line({});
    require(!options.use_warp, "default command line must select hardware");
    require(!options.show_help, "default command line must not request help");
    require(!options.frame_limit.has_value(), "default command line must have no frame limit");
}

void test_command_line_values()
{
    constexpr std::array arguments{std::wstring_view(L"--warp"), std::wstring_view(L"--frames"), std::wstring_view(L"120")};
    const daedalus::CommandLineOptions options = daedalus::parse_command_line(arguments);
    require(options.use_warp, "--warp must be recognized");
    require(options.frame_limit == 120, "--frames value must be parsed");
}

void test_command_line_rejections()
{
    constexpr std::array zero_frames{std::wstring_view(L"--frames"), std::wstring_view(L"0")};
    require_throws<daedalus::CommandLineError>(
        [&] { static_cast<void>(daedalus::parse_command_line(zero_frames)); },
        "zero frame count must be rejected");

    constexpr std::array duplicate{
        std::wstring_view(L"--frames"), std::wstring_view(L"1"), std::wstring_view(L"--frames"), std::wstring_view(L"2")};
    require_throws<daedalus::CommandLineError>(
        [&] { static_cast<void>(daedalus::parse_command_line(duplicate)); },
        "duplicate frame limits must be rejected");

    constexpr std::array unknown{std::wstring_view(L"--unknown")};
    require_throws<daedalus::CommandLineError>(
        [&] { static_cast<void>(daedalus::parse_command_line(unknown)); },
        "unknown options must be rejected");
}

void test_result_formatting()
{
    require(daedalus::format_result_code(static_cast<daedalus::ResultCode>(0x80004005U)).starts_with("0x80004005"),
            "result code formatting must preserve all hexadecimal digits");
    const std::string message = daedalus::format_failure_message(-1, "synthetic operation");
    require(message.find("synthetic operation") != std::string::npos, "failure message must include the operation");
    require(message.find("0xFFFFFFFF") != std::string::npos, "failure message must include the result code");

    try
    {
        daedalus::throw_if_failed(-1, "tested expression");
        throw TestFailure("negative result must throw");
    }
    catch (const daedalus::ResultError& error)
    {
        require(error.result() == -1, "ResultError must preserve the result value");
        require(error.operation() == "tested expression", "ResultError must preserve the operation");
    }
}

void test_adapter_policy()
{
    constexpr std::array candidates{
        daedalus::AdapterCandidate{true, false, 2ULL * 1024 * 1024 * 1024, 0},
        daedalus::AdapterCandidate{true, false, 8ULL * 1024 * 1024 * 1024, 1},
        daedalus::AdapterCandidate{true, true, 64ULL * 1024 * 1024 * 1024, 2},
        daedalus::AdapterCandidate{false, false, 128ULL * 1024 * 1024 * 1024, 3}};
    require(daedalus::choose_adapter_index(candidates) == 1, "largest suitable hardware adapter must win");

    constexpr std::array unsuitable{
        daedalus::AdapterCandidate{false, false, 4ULL * 1024 * 1024 * 1024, 0},
        daedalus::AdapterCandidate{true, true, 0, 1}};
    require_throws<std::runtime_error>(
        [&] { static_cast<void>(daedalus::choose_adapter_index(unsuitable)); },
        "policy must reject a list without suitable hardware");
}
}

int main()
{
    const std::vector<std::pair<std::string_view, std::function<void()>>> tests{
        {"command line defaults", test_command_line_defaults},
        {"command line values", test_command_line_values},
        {"command line rejections", test_command_line_rejections},
        {"result formatting", test_result_formatting},
        {"adapter policy", test_adapter_policy}};

    std::size_t failures = 0;
    for (const auto& [name, test] : tests)
    {
        try
        {
            test();
            std::cout << "[PASS] " << name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failures;
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
        }
    }

    if (failures != 0)
    {
        std::cerr << failures << " test case(s) failed\n";
        return 1;
    }

    std::cout << tests.size() << " test case(s) passed\n";
    return 0;
}

#pragma once

#include <cmath>
#include <exception>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace daedalus::tests
{
class Failure final : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

inline void require(bool condition, std::string_view message)
{
    if (!condition) throw Failure(std::string(message));
}

inline void require_near(float actual, float expected, float tolerance, std::string_view message)
{
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
        throw Failure(std::string(message) + " actual=" + std::to_string(actual) + " expected=" + std::to_string(expected));
}

template <typename Exception, typename Callable>
void require_throws(Callable&& callable, std::string_view message)
{
    try { std::invoke(std::forward<Callable>(callable)); }
    catch (const Exception&) { return; }
    catch (...) { throw Failure(std::string(message) + " (wrong exception type)"); }
    throw Failure(std::string(message) + " (no exception)");
}

inline int run(const std::vector<std::pair<std::string_view, std::function<void()>>>& tests)
{
    std::size_t failures = 0;
    for (const auto& [name, test] : tests)
    {
        try { test(); std::cout << "[PASS] " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "[FAIL] " << name << ": " << error.what() << '\n'; }
    }
    if (failures != 0) { std::cerr << failures << " test case(s) failed\n"; return 1; }
    std::cout << tests.size() << " test case(s) passed\n";
    return 0;
}
}

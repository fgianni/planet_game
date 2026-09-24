#pragma once

#include <cmath>
#include <exception>
#include <iostream>
#include <string_view>

namespace planetsim::test {

class Context {
public:
    void expect(bool condition,
                std::string_view expression,
                std::string_view file,
                int line) {
        if (condition) {
            return;
        }
        ++failures_;
        std::cerr << file << ':' << line << ": expectation failed: " << expression << '\n';
    }

    void expect_near(double actual,
                     double expected,
                     double tolerance,
                     std::string_view expression,
                     std::string_view file,
                     int line) {
        const bool result = std::isfinite(actual) && std::isfinite(expected) &&
                            std::abs(actual - expected) <= tolerance;
        if (result) {
            return;
        }
        ++failures_;
        std::cerr << file << ':' << line << ": expectation failed: " << expression
                  << " (actual=" << actual << ", expected=" << expected
                  << ", tolerance=" << tolerance << ")\n";
    }

    template <typename Exception, typename Function>
    void expect_throws(Function&& function,
                       std::string_view expression,
                       std::string_view file,
                       int line) {
        try {
            function();
        } catch (const Exception&) {
            return;
        } catch (const std::exception& exception) {
            ++failures_;
            std::cerr << file << ':' << line << ": expectation failed: " << expression
                      << " (wrong exception: " << exception.what() << ")\n";
            return;
        } catch (...) {
            ++failures_;
            std::cerr << file << ':' << line << ": expectation failed: " << expression
                      << " (wrong non-standard exception)\n";
            return;
        }
        ++failures_;
        std::cerr << file << ':' << line << ": expectation failed: " << expression
                  << " (no exception)\n";
    }

    [[nodiscard]] int result() const noexcept {
        if (failures_ == 0) {
            std::cout << "all expectations passed\n";
            return 0;
        }
        std::cerr << failures_ << " expectation(s) failed\n";
        return 1;
    }

private:
    int failures_ = 0;
};

}  // namespace planetsim::test

#define PLANETSIM_EXPECT(context, expression) \
    (context).expect((expression), #expression, __FILE__, __LINE__)

#define PLANETSIM_EXPECT_NEAR(context, actual, expected, tolerance) \
    (context).expect_near((actual), (expected), (tolerance), \
                          #actual " near " #expected, __FILE__, __LINE__)

#define PLANETSIM_EXPECT_THROWS(context, exception_type, expression) \
    (context).expect_throws<exception_type>([&]() { static_cast<void>(expression); }, \
                                            #expression, __FILE__, __LINE__)

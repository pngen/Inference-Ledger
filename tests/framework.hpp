// framework.hpp
// Tiny, header-only deterministic test framework.
// Apache License 2.0. Copyright 2026 Summon Software Labs.
#pragma once

#include <cmath>
#include <cstddef>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace testfw {

struct TestCase {
  std::string name;
  std::function<void()> fn;
};

inline std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}
inline int& failure_count() { static int n = 0; return n; }
inline int& pass_count() { static int n = 0; return n; }

inline void register_case(const std::string& name, std::function<void()> fn) {
  registry().push_back({name, std::move(fn)});
}

inline void report_failure(const std::string& expr, const std::string& file,
                           int line, const std::string& extra) {
  ++failure_count();
  std::cerr << "    FAIL[" << file << ":" << line << "] " << expr;
  if (!extra.empty()) std::cerr << "  // " << extra;
  std::cerr << "\n";
}

inline int execute() {
  for (auto& tc : registry()) {
    const int before = failure_count();
    std::cout << "[ RUN  ] " << tc.name << "\n";
    try {
      tc.fn();
    } catch (const std::exception& ex) {
      ++failure_count();
      std::cerr << "    EXCEPTION: " << ex.what() << "\n";
    } catch (...) {
      ++failure_count();
      std::cerr << "    EXCEPTION: unknown\n";
    }
    const bool ok = (failure_count() == before);
    if (ok) ++pass_count();
    std::cout << "[ " << (ok ? "PASS " : "FAIL") << " ] " << tc.name << "\n";
  }
  std::cout << "\n== " << pass_count() << " passed, " << failure_count()
            << " failed out of " << registry().size() << " ==\n";
  return failure_count() == 0 ? 0 : 1;
}

}  // namespace testfw

#define TEST(name) \
  static void test_fn_##name(); \
  static struct test_register_##name { \
    test_register_##name() { \
      ::testfw::register_case(#name, &test_fn_##name); \
    } \
  } test_register_inst_##name; \
  static void test_fn_##name()

#define CHECK(cond) \
  do { \
    if (!(cond)) ::testfw::report_failure(#cond, __FILE__, __LINE__, ""); \
  } while (0)

#define CHECK_MSG(cond, msg) \
  do { \
    if (!(cond)) ::testfw::report_failure(#cond, __FILE__, __LINE__, (msg)); \
  } while (0)

#define CHECK_EQ(a, b) \
  do { \
    const auto va = (a); const auto vb = (b); \
    if (!(va == vb)) { \
      std::ostringstream oss_; oss_ << va << " != " << vb; \
      ::testfw::report_failure(#a " == " #b, __FILE__, __LINE__, oss_.str()); \
    } \
  } while (0)

#define CHECK_NEAR(a, b, tol) \
  do { \
    const double va = static_cast<double>(a); \
    const double vb = static_cast<double>(b); \
    if (std::fabs(va - vb) > (tol)) { \
      std::ostringstream oss_; oss_ << va << " !~ " << vb; \
      ::testfw::report_failure(#a " ~= " #b, __FILE__, __LINE__, oss_.str()); \
    } \
  } while (0)

#define CHECK_THROWS(expr) \
  do { \
    bool threw_ = false; \
    try { (void)(expr); } catch (...) { threw_ = true; } \
    if (!threw_) ::testfw::report_failure(#expr " throws", __FILE__, __LINE__, ""); \
  } while (0)

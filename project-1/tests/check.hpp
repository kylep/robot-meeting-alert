// Minimal test harness: CHECK records failures; main returns the count.
#pragma once

#include <cstdio>

inline int g_failures = 0;

#define CHECK(expr)                                                     \
  do {                                                                  \
    if (!(expr)) {                                                      \
      ++g_failures;                                                     \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__,       \
                   __LINE__, #expr);                                    \
    }                                                                   \
  } while (0)

#define CHECK_STR(actual, expected)                                     \
  do {                                                                  \
    std::string a_ = (actual), e_ = (expected);                         \
    if (a_ != e_) {                                                     \
      ++g_failures;                                                     \
      std::fprintf(stderr, "%s:%d: got \"%s\", want \"%s\"\n",          \
                   __FILE__, __LINE__, a_.c_str(), e_.c_str());         \
    }                                                                   \
  } while (0)

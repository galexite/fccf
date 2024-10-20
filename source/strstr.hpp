#pragma once

#include <string_view>

#if defined(__x86_64__) || defined(__i386__)

size_t sse2_strstr_v2(const char *s, size_t n, const char *needle, size_t k);

inline size_t sse2_strstr_v2(const std::string_view haystack,
                             const std::string_view needle) {
  return sse2_strstr_v2(haystack.data(), haystack.size(), needle.data(),
                        needle.size());
}

#define HAVE_SIMD_STRSTR
#define STRSTR_IMPL sse2_strstr_v2

#elif defined(__aarch64__)

size_t aarch64_strstr_v2(const char *s, size_t n, const char *needle, size_t k);

inline size_t aarch64_strstr_v2(const std::string_view haystack,
                                const std::string_view needle) {
  return aarch64_strstr_v2(haystack.data(), haystack.size(), needle.data(),
                           needle.size());
}

#define HAVE_SIMD_STRSTR
#define STRSTR_IMPL aarch64_strstr_v2

#elif defined(__arm__) && defined(__NEON__)

size_t neon_strstr_v2(const char *s, size_t n, const char *needle, size_t k);

inline size_t neon_strstr_v2(const std::string_view haystack,
                             const std::string_view needle) {
  return neon_strstr_v2(haystack.data(), haystack.size(), needle.data(),
                        needle.size());
}

#define HAVE_SIMD_STRSTR
#define STRSTR_IMPL neon_strstr_v2

#endif

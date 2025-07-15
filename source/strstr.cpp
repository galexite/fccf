#include "strstr.hpp"

#ifdef HAVE_SIMD_STRSTR
#include <cassert>
#include <string>

#define FORCE_INLINE inline __attribute__((always_inline))
#define MAYBE_UNUSED inline __attribute__((unused))

#include "utils/bits.cpp"
#include "fixed-memcmp.cpp"
#endif

#if defined(__x86_64__) || defined(__i386__)
#include <immintrin.h>
#include "sse2-strstr.cpp"
#elif defined(__aarch64__)
#include <arm_neon.h>
#include "aarch64-strstr-v2.cpp"
#elif defined(__arm__) && defined(__NEON__)
#include <arm_neon.h>
#include "utils/neon.cpp"
#include "neon-strstr-v2.cpp"
#endif

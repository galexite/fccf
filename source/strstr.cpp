#include "strstr.hpp"

#ifdef HAVE_SIMD_STRSTR
#include <cassert>
#include <string>

#include "common.h"
#include "utils/bits.cpp"
#include "fixed-memcmp.cpp"
#endif

#if defined(__x86_64__) || defined(__i386__)
#include "sse2-strstr.cpp"
#elif defined(__aarch64__)
#include "aarch64-strstr-v2.cpp"
#elif defined(__arm__) && defined(__NEON__)
#include "utils/neon.cpp"
#include "neon-strstr-v2.cpp"
#endif

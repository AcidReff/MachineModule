#pragma once

namespace md::engine {
// The CMake option enables validated native Apple Silicon JIT by default.
// An explicit OFF build retains the interpreter fallback.
inline constexpr bool platformAllowsJit() {
#if defined(__APPLE__) && (defined(__aarch64__) || defined(__arm64__))
#if defined(MACHINEMODULE_ENABLE_ARM64_JIT) && MACHINEMODULE_ENABLE_ARM64_JIT
    return true;
#else
    return false;
#endif
#else
    return true;
#endif
}
}

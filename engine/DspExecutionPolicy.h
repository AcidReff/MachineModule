#pragma once

namespace md::engine {
// Native Apple Silicon JIT has a reported playback hang. Keep it experimental
// until its runtime behavior is verified on real ARM64 hardware.
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

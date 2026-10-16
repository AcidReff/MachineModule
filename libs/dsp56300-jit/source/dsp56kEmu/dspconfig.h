#pragma once

#include "dsp56kBase/buildconfig.h"

// MachineModule: explicit interpreter builds also disable internal JIT dispatch
// on native Apple Silicon, including interrupt paths.
#if defined(__APPLE__) && defined(HAVE_ARM64)
#if !defined(MACHINEMODULE_ENABLE_ARM64_JIT) || !MACHINEMODULE_ENABLE_ARM64_JIT
#ifndef DSP56K_FORCE_INTERPRETER
#define DSP56K_FORCE_INTERPRETER 1
#endif
#endif
#endif

namespace dsp56k
{
#ifdef DSP56K_AAR_TRANSLATE
	constexpr bool g_useAARTranslate = true;
#else
	constexpr bool g_useAARTranslate = false;
#endif

#if !defined(DSP56K_FORCE_INTERPRETER) && (defined(HAVE_X86_64) || defined(HAVE_ARM64))
	constexpr bool g_jitSupported = true;
#else
	constexpr bool g_jitSupported = false;
#endif

	// Safety net: validate the PC against the size of the JIT dispatch table before dispatching, see DSP::execJit().
	// Costs one compare + never-taken branch per dispatch. Set to false to get the old (unchecked) behaviour.
#ifdef DSP56K_NO_JIT_PC_GUARD
	constexpr bool g_jitPcGuard = false;
#else
	constexpr bool g_jitPcGuard = true;
#endif
}

#pragma once

#include <cstdint>
#include <string>

namespace dsp56k
{
	enum class ThreadPriority : int8_t
	{
		Lowest = -2,
		Low = -1,
		Normal = 0,
		High = 1,
		Highest = 2
	};

	class ThreadTools
	{
	public:
		static void setCurrentThreadName(const std::string& _name);
		static bool setCurrentThreadPriority(ThreadPriority _priority);
		// _samplerate/_blocksize describe the audio callback timing that paces the calling thread,
		// pass zeros if unknown to use conservative defaults. No-op on platforms other than macOS
		static bool setCurrentThreadRealtimeParameters(int _samplerate, int _blocksize);

		// Save/restore the calling thread's scheduling priority around a temporary boost. Only macOS
		// implements this (boot must briefly join the DSP workers' QoS band to share the P-cores); the
		// token is opaque and both calls are a no-op elsewhere. Restore with setCurrentThreadPriorityRaw.
		static uint32_t getCurrentThreadPriorityRaw();
		static void setCurrentThreadPriorityRaw(uint32_t _raw);

		// Put the calling thread in the same scheduling band as the host's audio
		// threads. On Windows it joins the MMCSS "Pro Audio" task, which DAWs
		// such as Ableton Live use for their engine threads (priority 23-26);
		// THREAD_PRIORITY_TIME_CRITICAL alone reaches only 15 in a normal
		// priority class process. If MMCSS refuses (it caps the number of
		// registered threads) or elsewhere, the thread gets Highest priority.
		// Returns a token for leaveProAudioTask, which must run on the same
		// thread before it exits so the MMCSS slot is released.
		static void* joinProAudioTask();
		static void leaveProAudioTask(void* _task);

		// Restrict the calling thread to the logical CPUs in _mask (bit n = CPU n).
		// Windows and Linux; returns false elsewhere or on failure.
		static bool setCurrentThreadAffinity(uint64_t _mask);

		// The logical CPU the calling thread runs on, -1 if unknown.
		static int getCurrentCpu();

		// For logical CPU _cpu: the logical CPUs of its physical core (SMT
		// siblings included) and of its last-level cache domain (an AMD CCX, for
		// example). Windows only (first 64 CPUs of processor group 0); returns
		// false elsewhere.
		static bool getCpuTopology(int _cpu, uint64_t& _coreMask, uint64_t& _cacheMask);
	};
}

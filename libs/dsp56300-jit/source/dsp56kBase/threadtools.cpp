#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "threadtools.h"

#include "logging.h"

#include <algorithm>

#ifdef DSP56K_USE_VTUNE_JIT_PROFILING_API
#include "vtuneSdk/include/ittnotify.h"
#endif

#ifdef _WIN32
#	include <Windows.h>
#else
#	include <cerrno>
#	include <pthread.h>
#	include <sched.h>
#	include <sys/resource.h>
#	include <sys/syscall.h>
#	include <unistd.h>
#	include <string.h>	// strerror
#endif

#ifdef __APPLE__
#	include <mach/mach.h>
#	include <mach/mach_time.h>
#endif

namespace dsp56k
{
#ifdef _WIN32
	constexpr DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push,8)
	typedef struct tagTHREADNAME_INFO
	{
		DWORD dwType; // Must be 0x1000.
		LPCSTR szName; // Pointer to name (in user addr space).
		DWORD dwThreadID; // Thread ID (-1=caller thread).
		DWORD dwFlags; // Reserved for future use, must be zero.
	} THREADNAME_INFO;
#pragma pack(pop)

	void SetThreadName( DWORD dwThreadID, const char* threadName)
	{
		THREADNAME_INFO info;
		info.dwType = 0x1000;
		info.szName = threadName;
		info.dwThreadID = dwThreadID;
		info.dwFlags = 0;

		__try  // NOLINT(clang-diagnostic-language-extension-token)
		{
			RaiseException( MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), reinterpret_cast<ULONG_PTR*>(&info) );
		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}

	// SetThreadDescription is Win10 1607+. Load it dynamically so we stay compatible with Win7.
	using SetThreadDescriptionFunc = HRESULT (WINAPI*)(HANDLE, PCWSTR);
	static SetThreadDescriptionFunc getSetThreadDescription()
	{
		static const auto s_func = []() -> SetThreadDescriptionFunc
		{
			if (const auto kernel32 = GetModuleHandleW(L"kernel32.dll"))
				return reinterpret_cast<SetThreadDescriptionFunc>(reinterpret_cast<void*>(GetProcAddress(kernel32, "SetThreadDescription")));
			return nullptr;
		}();
		return s_func;
	}
#endif
	void ThreadTools::setCurrentThreadName(const std::string& _name)
	{
#ifdef _WIN32
		SetThreadName(-1, _name.c_str());

		if (const auto setDesc = getSetThreadDescription())
		{
			const auto len = MultiByteToWideChar(CP_UTF8, 0, _name.c_str(), -1, nullptr, 0);
			if (len > 0)
			{
				std::wstring wide(static_cast<size_t>(len - 1), L'\0');
				MultiByteToWideChar(CP_UTF8, 0, _name.c_str(), -1, wide.data(), len);
				setDesc(GetCurrentThread(), wide.c_str());
			}
		}
#elif defined(__APPLE__)
		pthread_setname_np(_name.c_str());
#else
		pthread_setname_np(pthread_self(), _name.c_str());
#endif

#ifdef DSP56K_USE_VTUNE_JIT_PROFILING_API
		__itt_thread_set_name(_name.c_str());
#endif
	}

	bool ThreadTools::setCurrentThreadPriority(ThreadPriority _priority)
	{
#ifdef _WIN32
		int prio;
		switch(_priority)
		{
		case ThreadPriority::Lowest:	prio = THREAD_PRIORITY_LOWEST; break;
		case ThreadPriority::Low:		prio = THREAD_PRIORITY_BELOW_NORMAL; break;
		case ThreadPriority::Normal:	prio = THREAD_PRIORITY_NORMAL; break;
		case ThreadPriority::High:		prio = THREAD_PRIORITY_ABOVE_NORMAL; break;
		case ThreadPriority::Highest:	prio = THREAD_PRIORITY_TIME_CRITICAL; break;
		default: return false;
		}
		if( !::SetThreadPriority(GetCurrentThread(), prio))
		{
			LOG("Failed to set thread priority to " << prio);
			return false;
		}
#elif defined(__APPLE__)
		// Never call pthread_setschedparam() here. Doing so permanently opts the thread out of the
		// QOS class system, all subsequent pthread_set_qos_class_self_np() calls fail with EPERM.
		// QOS is what keeps threads off of the efficiency cores on Apple Silicon.
		qos_class_t qosClass;
		switch(_priority)
		{
		case ThreadPriority::Lowest:	qosClass = QOS_CLASS_BACKGROUND; break;
		case ThreadPriority::Low:		qosClass = QOS_CLASS_UTILITY; break;
		case ThreadPriority::Normal:	qosClass = QOS_CLASS_DEFAULT; break;
		case ThreadPriority::High:		qosClass = QOS_CLASS_USER_INITIATED; break;
		case ThreadPriority::Highest:	qosClass = QOS_CLASS_USER_INTERACTIVE; break;
		default: return false;
		}

		const auto result = pthread_set_qos_class_self_np(qosClass, 0);
		if (result != 0)
			LOG("Failed to set thread QOS class to " << qosClass << ": " << strerror(result));

		// Realtime workers additionally get a time constraint policy. This applies conservative
		// defaults, callers that know the audio block timing refine them via setCurrentThreadRealtimeParameters()
		if (_priority == ThreadPriority::Highest)
			return setCurrentThreadRealtimeParameters(0, 0) || result == 0;

		return result == 0;
#else
		// On Linux we adjust the 'nice' value of the thread
		int prio;
		switch (_priority)
		{
			case ThreadPriority::Lowest:	prio = 15;	break;
			case ThreadPriority::Low:		prio = 10;	break;
			case ThreadPriority::Normal:	prio = 0;	break;
			case ThreadPriority::High:		prio = -5;	break;
			case ThreadPriority::Highest:	prio = -10;	break;
			default:						return false;
		}
#ifdef PRIO_THREAD
		const auto result = setpriority(PRIO_THREAD, 0, prio);
#else
		const auto tid = syscall(SYS_gettid);
		if (!tid)
		{
			LOG("Failed to get thread id for setting priority");
			return false;
		}
		const auto result = setpriority(PRIO_PROCESS, tid, prio);
#endif
		if (result != 0)
		{
			LOG("Failed to set thread priority to " << prio << ": " << strerror(errno));
			return false;
		}
#endif
		return true;
	}

	bool ThreadTools::setCurrentThreadRealtimeParameters(int _samplerate, int _blocksize)
	{
#ifdef __APPLE__
		bool usePeriod = true;

		if (_samplerate <= 0 || _blocksize <= 0)
		{
			// the audio block timing is not known (yet). Base the values on a typical setup but do
			// not claim a fixed activation period as we do not know the real one
			_samplerate = 44100;
			_blocksize = 2048;
			usePeriod = false;
		}

		const double periodUsec = static_cast<double>(_blocksize) * 1'000'000.0 / static_cast<double>(_samplerate);

		// The audio thread wakes us once per block and we may need a large part of the block duration
		// to produce audio for it. Overstating the computation starves other realtime threads while
		// understating it causes the kernel to demote us out of the realtime band on overruns.
		double computationUsec = periodUsec * 0.5;
		double constraintUsec = periodUsec;

		// clamp to sane limits, the kernel rejects excessive values
		computationUsec = std::clamp(computationUsec, 250.0, 25'000.0);
		constraintUsec = std::clamp(constraintUsec, computationUsec + 250.0, 50'000.0);

		// thread_time_constraint_policy expects mach absolute time units, not microseconds
		mach_timebase_info_data_t timebase{};
		mach_timebase_info(&timebase);
		const double usecToAbs = 1000.0 * static_cast<double>(timebase.denom) / static_cast<double>(timebase.numer);

		thread_time_constraint_policy_data_t policy;
		policy.period      = static_cast<uint32_t>(usePeriod ? periodUsec * usecToAbs : 0.0);
		policy.computation = static_cast<uint32_t>(computationUsec * usecToAbs);
		policy.constraint  = static_cast<uint32_t>(constraintUsec * usecToAbs);
		policy.preemptible = TRUE;

		const thread_port_t thread = pthread_mach_thread_np(pthread_self());
		const kern_return_t result = thread_policy_set(thread,
		                                               THREAD_TIME_CONSTRAINT_POLICY,
		                                               reinterpret_cast<thread_policy_t>(&policy),
		                                               THREAD_TIME_CONSTRAINT_POLICY_COUNT);

		if (result == KERN_SUCCESS)
		{
			LOG("Set thread realtime parameters: period=" << (usePeriod ? periodUsec : 0.0) << " us, computation=" << computationUsec << " us, constraint=" << constraintUsec << " us");
			return true;
		}
		LOG("Failed to set thread realtime parameters, error code " << result);
#endif
		return false;
	}

	uint32_t ThreadTools::getCurrentThreadPriorityRaw()
	{
#ifdef __APPLE__
		qos_class_t qos = QOS_CLASS_UNSPECIFIED;
		pthread_get_qos_class_np(pthread_self(), &qos, nullptr);
		return static_cast<uint32_t>(qos);
#else
		return 0;
#endif
	}

	void ThreadTools::setCurrentThreadPriorityRaw(uint32_t _raw)
	{
#ifdef __APPLE__
		const auto qos = static_cast<qos_class_t>(_raw);
		if(qos != QOS_CLASS_UNSPECIFIED)
			pthread_set_qos_class_self_np(qos, 0);
#else
		(void)_raw;
#endif
	}

#ifdef _WIN32
	namespace
	{
		// avrt.dll is loaded on demand so that nothing links against it.
		using AvSetMmThreadCharacteristicsFunc = HANDLE (WINAPI*)(LPCWSTR, LPDWORD);
		using AvRevertMmThreadCharacteristicsFunc = BOOL (WINAPI*)(HANDLE);

		struct Avrt
		{
			AvSetMmThreadCharacteristicsFunc set = nullptr;
			AvRevertMmThreadCharacteristicsFunc revert = nullptr;

			Avrt()
			{
				if(const auto module = LoadLibraryW(L"avrt.dll"))
				{
					set = reinterpret_cast<AvSetMmThreadCharacteristicsFunc>(
						reinterpret_cast<void*>(GetProcAddress(module, "AvSetMmThreadCharacteristicsW")));
					revert = reinterpret_cast<AvRevertMmThreadCharacteristicsFunc>(
						reinterpret_cast<void*>(GetProcAddress(module, "AvRevertMmThreadCharacteristics")));
				}
			}
		};

		const Avrt& avrt()
		{
			static const Avrt s_avrt;
			return s_avrt;
		}
	}
#endif

	void* ThreadTools::joinProAudioTask()
	{
#ifdef _WIN32
		const auto& api = avrt();
		if(api.set)
		{
			DWORD taskIndex = 0;
			if(const auto handle = api.set(L"Pro Audio", &taskIndex))
				return handle;
			LOG("Failed to join the MMCSS Pro Audio task, error " << GetLastError());
		}
#endif
		setCurrentThreadPriority(ThreadPriority::Highest);
		return nullptr;
	}

	void ThreadTools::leaveProAudioTask(void* _task)
	{
#ifdef _WIN32
		if(_task && avrt().revert)
			avrt().revert(_task);
#else
		(void)_task;
#endif
	}

	bool ThreadTools::setCurrentThreadAffinity(const uint64_t _mask)
	{
		if(!_mask)
			return false;
#ifdef _WIN32
		return SetThreadAffinityMask(GetCurrentThread(), static_cast<DWORD_PTR>(_mask)) != 0;
#elif defined(__linux__)
		cpu_set_t set;
		CPU_ZERO(&set);
		for(int i = 0; i < 64; ++i)
		{
			if(_mask & (uint64_t{1} << i))
				CPU_SET(i, &set);
		}
		return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
#else
		return false;
#endif
	}

	int ThreadTools::getCurrentCpu()
	{
#ifdef _WIN32
		return static_cast<int>(GetCurrentProcessorNumber());
#elif defined(__linux__)
		return sched_getcpu();
#else
		return -1;
#endif
	}

	bool ThreadTools::getCpuTopology(const int _cpu, uint64_t& _coreMask, uint64_t& _cacheMask)
	{
		_coreMask = _cacheMask = 0;
#ifdef _WIN32
		if(_cpu < 0 || _cpu >= 64)
			return false;
		DWORD size = 0;
		GetLogicalProcessorInformationEx(RelationAll, nullptr, &size);
		if(!size)
			return false;
		std::string buffer(size, '\0');
		auto* const first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
		if(!GetLogicalProcessorInformationEx(RelationAll, first, &size))
			return false;
		const auto bit = KAFFINITY{1} << _cpu;
		uint32_t cacheLevel = 0;
		for(DWORD offset = 0; offset < size;)
		{
			const auto* info = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
			if(info->Relationship == RelationProcessorCore)
			{
				const auto& mask = info->Processor.GroupMask[0];
				if(mask.Group == 0 && (mask.Mask & bit))
					_coreMask = mask.Mask;
			}
			else if(info->Relationship == RelationCache && info->Cache.Level >= cacheLevel
				&& info->Cache.GroupMask.Group == 0 && (info->Cache.GroupMask.Mask & bit))
			{
				cacheLevel = info->Cache.Level;
				_cacheMask = info->Cache.GroupMask.Mask;
			}
			offset += info->Size;
		}
		return _coreMask && _cacheMask;
#else
		(void)_cpu;
		return false;
#endif
	}
}

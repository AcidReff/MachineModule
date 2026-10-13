#include "ParallelVoiceEngine.h"

#include <algorithm>
#include <chrono>

namespace md::engine
{
	ParallelVoiceEngine::ParallelVoiceEngine(const fw::Firmware& _fw, const int _groups)
	{
		const int n = std::clamp(_groups, 1, kVoices);
		m_groups.reserve(static_cast<size_t>(n));
		for(int g = 0; g < n; ++g)
			m_groups.push_back(std::make_unique<VoiceEngine>(_fw));
		m_groupOut.resize(static_cast<size_t>(n));
		for(int v = 0; v < kVoices; ++v) m_map[static_cast<size_t>(v)] = v % n;
		m_ok.assign(static_cast<size_t>(n), 0);
		groupUs.assign(static_cast<size_t>(n), 0.0);
		groupVoices.assign(static_cast<size_t>(n), 0.0);
		for(size_t g = 1; g < static_cast<size_t>(n); ++g)
			m_workers.emplace_back([this, g] { workerMain(g); });
	}

	ParallelVoiceEngine::~ParallelVoiceEngine()
	{
		{
			std::lock_guard<std::mutex> l(m_mx);
			m_stop = true;
		}
		m_wake.notify_all();
		for(auto& t : m_workers)
			t.join();
	}

	void ParallelVoiceEngine::runGroup(const size_t _g)
	{
		const auto t0 = timingOn ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		m_ok[_g] = m_groups[_g]->renderBlock(m_groupOut[_g]) ? 1 : 0;
		if(m_ok[_g] && m_post)
			m_post(static_cast<int>(_g), m_groupOut[_g]);
		if(timingOn)
		{
			groupUs[_g] += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
			groupVoices[_g] += m_groups[_g]->activeVoicesLastBlock();
		}
	}

	void ParallelVoiceEngine::tuneWorkers(std::function<void(int)> _fn)
	{
		std::lock_guard<std::mutex> l(m_mx);
		m_tune = std::move(_fn);
		++m_tuneGen;
	}

	void ParallelVoiceEngine::workerMain(const size_t _g)
	{
		uint64_t seen = 0, tuned = 0;
		for(;;)
		{
			std::function<void(int)> tune;
			{
				std::unique_lock<std::mutex> l(m_mx);
				m_wake.wait(l, [&] { return m_stop || m_gen != seen; });
				if(m_stop)
					return;
				seen = m_gen;
				if(tuned != m_tuneGen)
				{
					tuned = m_tuneGen;
					tune = m_tune;
				}
			}
			if(tune)
				tune(static_cast<int>(_g));
			runGroup(_g);
			{
				std::lock_guard<std::mutex> l(m_mx);
				if(--m_pending == 0)
					m_done.notify_one();
			}
		}
	}

	void ParallelVoiceEngine::writeP(const uint32_t _addr, const uint32_t* _words, const size_t _count)
	{
		for(auto& g : m_groups)
			g->writeP(_addr, _words, _count);
	}

	int ParallelVoiceEngine::activeVoicesLastBlock() const
	{
		int n = 0;
		for(const auto& g : m_groups)
			n += g->activeVoicesLastBlock();
		return n;
	}

	void ParallelVoiceEngine::setSlot(const int _voice, const uint32_t* _words, const int _count)
	{
		m_groups[static_cast<size_t>(groupOf(_voice))]->setSlot(_voice, _words, _count);
	}

	bool ParallelVoiceEngine::renderBlock(Block& _out)
	{
		const auto n = m_groups.size();
		auto& ok = m_ok;
		if(n > 1)
		{
			{
				std::lock_guard<std::mutex> l(m_mx);
				m_pending = n - 1;
				++m_gen;
			}
			m_wake.notify_all();
		}
		runGroup(0);
		if(n > 1)
		{
			std::unique_lock<std::mutex> l(m_mx);
			m_done.wait(l, [&] { return m_pending == 0; });
		}

		m_lastInstructions = 0;
		m_fault.clear();
		bool allOk = true;
		for(size_t g = 0; g < n; ++g)
		{
			m_lastInstructions += m_groups[g]->instructionsLastBlock();
			if(!ok[g])
			{
				allOk = false;
				if(m_fault.empty())
					m_fault = m_groups[g]->faultReason();
			}
		}
		if(!allOk)
			return false;

		for(int v = 0; v < kVoices; ++v)
			_out[v] = m_groupOut[static_cast<size_t>(groupOf(v))][v];
		return true;
	}
}

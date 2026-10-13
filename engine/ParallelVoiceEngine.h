// Splits the 16 voice slots across several VoiceEngine instances rendered in parallel threads, one physical
// DSP2 emulation per group. Each instance only ever receives setSlot() calls for the voices assigned to it;
// the rest stay at machine 0 (never triggered) forever, which VoiceEngine's own harness already renders as a
// fast, near-free clear (see VoiceEngine::installHarness). So each instance's cost is roughly the shared
// per-block baseline plus just its own subset's active-voice cost, and those run concurrently on separate
// cores instead of all 16 voices' cost serialized on one.
//
// This does not change what gets emulated (DSP2 is still one program run per group, exactly as VoiceEngine
// runs it alone), only how the 16 slots' work is scheduled across cores. See HANDOFF.md, "per-machine cost
// profiled" for the measurement this answers.
#pragma once
#include <array>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "VoiceEngine.h"

namespace md::engine
{
	class ParallelVoiceEngine
	{
	public:
		static constexpr int kVoices = VoiceEngine::kVoices;
		static constexpr int kBlockFrames = VoiceEngine::kBlockFrames;
		static constexpr int kSlotWords = VoiceEngine::kSlotWords;
		using Block = VoiceEngine::Block;

		// _groups: how many VoiceEngine instances to split the 16 voices across (each on its own thread during
		// renderBlock). 1 disables parallelism (behaves like VoiceEngine, minus the thread-per-block overhead).
		ParallelVoiceEngine(const fw::Firmware& _fw, int _groups);
		~ParallelVoiceEngine();

		// Run _fn(group) once on each worker thread (group 1..n-1), on its next block: set its name, priority and
		// core there (real-time priority only once booted, like the caller's own thread).
		void tuneWorkers(std::function<void(int)> _fn);

		void setSlot(int _voice, const uint32_t* _words, int _count = kSlotWords);
		bool renderBlock(Block& _out);

		// Work to run on each group's own thread right after its voices render (the engine's per-track effects:
		// tracks t with groupOf(t) == group). Gets the group's block, valid for its own voices only.
		using PostFn = std::function<void(int _group, const Block&)>;
		void setPost(PostFn _fn) { m_post = std::move(_fn); }
		// Stats (only meaningful with timing on): per group, microseconds spent (voices + track effects) and voices rendered,
		// summed over blocks; reset by the caller.
		bool timingOn = false;
		std::vector<double> groupUs, groupVoices;
		int groupCount() const { return static_cast<int>(m_groups.size()); }
		int groupOf(int _voice) const { return m_map[static_cast<size_t>(_voice)]; }

		// Move a voice to another group (call between blocks, from the thread that calls renderBlock). The old group's
		// copy is silenced with _silence (the empty-machine trigger slot) so it stops costing time there; the voice
		// starts fresh in the new group on its next trigger.
		void moveVoice(int _voice, int _group, const uint32_t* _silence, int _count)
		{
			const int old = m_map[static_cast<size_t>(_voice)];
			if(old == _group) return;
			m_groups[static_cast<size_t>(old)]->setSlot(_voice, _silence, _count);
			m_map[static_cast<size_t>(_voice)] = _group;
		}

		// Sample memory etc: written to every group (each has its own P memory).
		void writeP(uint32_t _addr, const uint32_t* _words, size_t _count);
		uint32_t readP(uint32_t _addr) const { return m_groups[0]->readP(_addr); }
		int activeVoicesLastBlock() const;
		uint32_t voiceInstructions(int _voice) const { return m_groups[static_cast<size_t>(groupOf(_voice))]->voiceInstructions(_voice); }
		dsp56k::DSP& dsp() { return m_groups[0]->dsp(); }

		uint64_t instructionsLastBlock() const { return m_lastInstructions; }
		const std::string& faultReason() const { return m_fault; }

	private:
		PostFn m_post;
		std::array<int, kVoices> m_map{};	// voice -> group

		void workerMain(size_t _g);
		void runGroup(size_t _g);

		// Persistent workers (a thread spawned per block rebooted the device): woken once per block, they render
		// their group while the caller renders group 0, then the caller waits for them.
		std::vector<std::thread> m_workers;
		std::mutex m_mx;
		std::condition_variable m_wake, m_done;
		uint64_t m_gen = 0, m_tuneGen = 0;
		std::function<void(int)> m_tune;
		size_t m_pending = 0;
		bool m_stop = false;
		std::vector<uint8_t> m_ok;

		std::vector<std::unique_ptr<VoiceEngine>> m_groups;
		std::vector<Block> m_groupOut;
		uint64_t m_lastInstructions = 0;
		std::string m_fault;
	};
}

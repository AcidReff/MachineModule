#pragma once

#include <map>

#include "types.h"

namespace dsp56k
{
	class JitBlockRuntimeData;
	class Jit;

	struct JitCacheEntry
	{
		using SingleOpMap = std::map<uint64_t, JitBlockRuntimeData*>;
		using SingleOpMapIt = SingleOpMap::iterator;

		JitCacheEntry() = default;
		JitCacheEntry(const JitCacheEntry&) = delete;
		JitCacheEntry(JitCacheEntry&& _e) noexcept
		{
			block = _e.block;
			singleOpCache = _e.singleOpCache;
			_e.block = nullptr;
			_e.singleOpCache = nullptr;
		}

		JitCacheEntry& operator = (const JitCacheEntry&) = delete;
		JitCacheEntry& operator = (JitCacheEntry&&) = delete;

		~JitCacheEntry()
		{
			delete singleOpCache;
		}

		SingleOpMapIt findSingleOp(const uint64_t _key) const
		{
			if(!singleOpCache)
				return {};

			return singleOpCache->find(_key);
		}

		JitBlockRuntimeData* takeSingleOp(const SingleOpMapIt& _it)
		{
			if(!singleOpCache || _it == singleOpCache->end())
				return nullptr;
			auto* const block = _it->second;
			// Keep the map node as an empty slot. Self-modifying code repeatedly
			// alternates between executing and caching the same instruction; erasing
			// here forced a map-node allocation every time the block returned.
			_it->second = nullptr;
			return block;
		}

		bool addSingleOp(const uint64_t _key, JitBlockRuntimeData* _block)
		{
			if(!singleOpCache)
				singleOpCache = new SingleOpMap();
			const auto [it, inserted] = singleOpCache->insert(std::make_pair(_key, _block));
			if(inserted)
				return true;
			if(it->second != nullptr)
				return false;
			it->second = _block;
			return true;
		}

		bool isValid(const SingleOpMapIt& _it) const
		{
			return singleOpCache != nullptr && _it != singleOpCache->end()
				&& _it->second != nullptr;
		}

		JitBlockRuntimeData* block = nullptr;
		SingleOpMap* singleOpCache = nullptr;
	};
}

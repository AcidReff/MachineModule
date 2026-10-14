#pragma once

#include <algorithm>
#include <cassert>
#include <memory>
#include <vector>

namespace dsp56k
{
	// Sparse metadata storage. Reads of untouched pages return an immutable
	// default object; writes construct one page without moving existing objects.
	// Unlike the dispatch table, this array is never indexed by generated code.
	template<typename T, size_t PageSize = 1024>
	class PagedArray
	{
	public:
		void init(size_t _maxSize)
		{
			clear();
			m_maxSize = _maxSize;
			m_pages.resize((_maxSize + PageSize - 1) / PageSize);
		}

		const T& operator[](size_t _index) const
		{
			if (_index >= m_size)
				return m_default;
			const auto& page = m_pages[_index / PageSize];
			return page ? page[_index % PageSize] : m_default;
		}

		T& edit(size_t _index)
		{
			ensureBlockForIndex(_index);
			return m_pages[_index / PageSize][_index % PageSize];
		}

		bool ensureBlockForIndex(size_t _index)
		{
			assert(_index < m_maxSize);
			auto& page = m_pages[_index / PageSize];
			if (page)
				return false;
			page = std::make_unique<T[]>(PageSize);
			m_size = std::max(m_size, std::min(m_maxSize, (_index / PageSize + 1) * PageSize));
			return true;
		}

		template<typename F> void forEachAllocated(F&& _visit)
		{
			for (size_t p = 0; p < m_pages.size(); ++p)
			{
				if (!m_pages[p])
					continue;
				const auto count = std::min(PageSize, m_maxSize - p * PageSize);
				for (size_t i = 0; i < count; ++i)
					_visit(m_pages[p][i]);
			}
		}

		size_t size() const { return m_size; }
		void clear()
		{
			m_pages.clear();
			m_size = 0;
			m_maxSize = 0;
		}

	private:
		std::vector<std::unique_ptr<T[]>> m_pages;
		const T m_default{};
		size_t m_size = 0;
		size_t m_maxSize = 0;
	};
}

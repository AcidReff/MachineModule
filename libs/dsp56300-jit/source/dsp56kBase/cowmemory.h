#pragma once

#include <cstddef>
#include <cstdint>

namespace dsp56k
{
	// A whole anonymous allocation, cloned privately by the macOS VM. No fixed
	// address replacement or partial unmapping (the old MMU path's EXC_GUARD
	// failure) is involved. Unsupported platforms report failure to the caller.
	class CowMemory
	{
	public:
		CowMemory() = default;
		~CowMemory();
		CowMemory(const CowMemory&) = delete;
		CowMemory& operator=(const CowMemory&) = delete;

		bool allocate(size_t _bytes);
		bool clone(const CowMemory& _source);
		void clear();
		void* data() { return m_data; }
		const void* data() const { return m_data; }
		size_t size() const { return m_size; }

	private:
		void* m_data = nullptr;
		size_t m_size = 0;
	};
}

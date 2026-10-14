#include "pagedarray.h"

#include <cstdio>
#include <cstdlib>

namespace
{
	struct Entry
	{
		Entry() { ++live; ++constructed; }
		~Entry() { --live; }
		Entry(const Entry&) = delete;
		Entry(Entry&&) = delete;
		int value = 0;
		static int live;
		static int constructed;
	};
	int Entry::live = 0;
	int Entry::constructed = 0;
	void check(bool _ok, const char* _message)
	{
		if (!_ok) { std::fprintf(stderr, "%s\n", _message); std::exit(1); }
	}
}

int main()
{
	{
		dsp56k::PagedArray<Entry, 64> entries;
		entries.init(0x800003);
		check(Entry::constructed == 1, "init constructed sparse entries");
		check(entries[0x800002].value == 0, "untouched read is not default");
		auto* low = &entries.edit(7);
		low->value = 42;
		entries.edit(0x800002).value = 99;
		check(Entry::constructed == 129, "high address constructed intervening pages");
		check(&entries[7] == low && low->value == 42, "growth moved existing entries");
		check(entries[0x400000].value == 0, "sparse hole lost default value");
		check(entries.size() == 0x800003, "last partial page exceeds maximum");
		int visited = 0;
		entries.forEachAllocated([&](Entry&) { ++visited; });
		check(visited == 67, "iteration visited holes or past end");
		check(!entries.ensureBlockForIndex(6), "existing page was reallocated");
		entries.clear();
		check(Entry::live == 1 && entries.size() == 0, "clear did not destroy all entries");
		entries.init(65);
		entries.edit(64).value = 1;
		check(entries[7].value == 0, "reinitialization retained stale entries");
	}
	check(Entry::live == 0, "destruction leaked entries");
	std::puts("Paged array sparse allocation, stable references and lifetime checks passed");
}

#include "cowmemory.h"
#include "mmuarray.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

namespace
{
	void check(bool _condition, const char* _message)
	{
		if (!_condition) { std::fprintf(stderr, "%s\n", _message); std::exit(1); }
	}
	void initial(unsigned& _value) noexcept { _value = 17; }
	void modified(unsigned& _value) noexcept { _value = 31; }
	using Func = void (*)(unsigned&) noexcept;
#ifdef __APPLE__
	uint64_t footprint()
	{
		task_vm_info_data_t info{};
		mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
		check(task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count)
			== KERN_SUCCESS, "could not measure VM footprint");
		return info.phys_footprint;
	}
#endif
}

int main()
{
	dsp56k::CowMemory empty, source;
	check(!source.allocate(0) && !source.clone(empty), "empty mapping was accepted");
#ifndef __APPLE__
	check(!source.allocate(4096), "unsupported platform did not select fallback");
	return 77;
#else
	constexpr size_t count = 0x800000;
	const auto before = footprint();
	check(source.allocate(count * sizeof(Func)), "template allocation failed");
	auto* original = static_cast<Func*>(source.data());
	std::uninitialized_fill_n(original, count, &initial);
	check(!source.clone(source) && source.data() == original, "self-clone lost its source");
	const auto withTemplate = footprint();
	std::vector<std::unique_ptr<dsp56k::MmuArray<Func>>> clones;
	std::vector<double> duration;
	for (unsigned n = 0; n < 32; ++n)
	{
		auto copy = std::make_unique<dsp56k::MmuArray<Func>>();
		const auto start = std::chrono::steady_clock::now();
		check(copy->initFromTemplate(source), "private mapping failed");
		duration.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
		check(copy->size() == count && copy->data() != original, "clone extent/base is invalid");
		unsigned value = 0;
		(*copy)[count - 1](value);
		check(value == 17, "cloned function pointer did not execute");
		(*copy)[n] = &modified;
		(*copy)[count - 1] = &modified;
		check(original[n] == &initial && original[count - 1] == &initial, "clone modified template");
		if (n) check((*clones.front())[n] == &initial, "write leaked into sibling clone");
		auto* base = copy->data();
		check(!copy->ensureBlockForIndex(count - 1) && !copy->ensureSize(count - 1)
			&& copy->data() == base, "mapped table was reallocated or cleared");
		clones.push_back(std::move(copy));
	}
	const auto withClones = footprint();
	check(withClones < withTemplate + count * sizeof(Func), "clones committed a dense table each");
	source.clear();
	check(!source.data() && !source.size(), "template clear retained mapping");
	// Clones own their lifetime independently of the template and one another.
	for (unsigned n = 0; n < clones.size(); ++n)
	{
		unsigned value = 0;
		(*clones[n])[n](value);
		check(value == 31 && (*clones[n])[1000] == &initial, "template deletion damaged clone");
	}
	for (size_t n = 1; n < clones.size(); n += 2) clones[n].reset();
	clones.clear();
	// A failed clone must leave a clean destination, allowing vector fallback.
	dsp56k::MmuArray<Func> fallback;
	check(!fallback.initFromTemplate(empty), "empty template did not request fallback");
	fallback.init(4096, &initial);
	fallback.ensureBlockForIndex(2048);
	check(fallback[2048] == &initial, "fallback table initialization failed");
	fallback.clear();
	check(!fallback.data() && !fallback.size(), "fallback clear failed");
	const auto after = footprint();
	check(after < withTemplate, "destroyed clones retained the entire template footprint");
	std::sort(duration.begin(), duration.end());
	std::printf("COW bytes: baseline=%llu template=%llu clones=%llu after=%llu; clone median=%.3f us max=%.3f us\n",
		static_cast<unsigned long long>(before), static_cast<unsigned long long>(withTemplate),
		static_cast<unsigned long long>(withClones), static_cast<unsigned long long>(after), duration[16], duration.back());
	std::puts("COW isolation, independent lifetime, bounded commitment and fallback passed");
	return 0;
#endif
}

#include "cowmemory.h"

#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

namespace dsp56k
{
	CowMemory::~CowMemory() { clear(); }

	bool CowMemory::allocate(const size_t _bytes)
	{
		clear();
#ifdef __APPLE__
		if (!_bytes) return false;
		mach_vm_address_t address = 0;
		if (mach_vm_allocate(mach_task_self(), &address, _bytes, VM_FLAGS_ANYWHERE) != KERN_SUCCESS)
			return false;
		m_data = reinterpret_cast<void*>(address);
		m_size = _bytes;
		return true;
#else
		(void)_bytes;
		return false;
#endif
	}

	bool CowMemory::clone(const CowMemory& _source)
	{
		if (this == &_source) return false;
		clear();
#ifdef __APPLE__
		if (!_source.data()) return false;
		mach_vm_address_t address = 0;
		vm_prot_t current = 0, maximum = 0;
		const auto result = mach_vm_remap(mach_task_self(), &address, _source.size(), 0,
			VM_FLAGS_ANYWHERE, mach_task_self(), reinterpret_cast<mach_vm_address_t>(_source.data()),
			TRUE, &current, &maximum, VM_INHERIT_NONE);
		if (result != KERN_SUCCESS) return false;
		m_data = reinterpret_cast<void*>(address);
		m_size = _source.size();
		if ((current & (VM_PROT_READ | VM_PROT_WRITE)) != (VM_PROT_READ | VM_PROT_WRITE))
		{
			clear();
			return false;
		}
		return true;
#else
		(void)_source;
		return false;
#endif
	}

	void CowMemory::clear()
	{
#ifdef __APPLE__
		if (m_data)
			mach_vm_deallocate(mach_task_self(), reinterpret_cast<mach_vm_address_t>(m_data), m_size);
#endif
		m_data = nullptr;
		m_size = 0;
	}
}

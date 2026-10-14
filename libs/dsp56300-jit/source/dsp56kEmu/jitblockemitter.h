#pragma once

#include "jitblock.h"
#include "jitemitter.h"

#include "asmjit/core/codeholder.h"

#include <cstdlib>

namespace dsp56k
{
	struct JitConfig;
	struct JitRuntimeData;
	class DSP;

	struct JitBlockEmitter
	{
		static constexpr size_t MaxRetainedTextBufferBytes = 64 * 1024;

		JitBlockEmitter(DSP& _dsp, JitRuntimeData& _runtimeData, JitConfig&& _config)
		: emitter(nullptr)
		, block(emitter, _dsp, _runtimeData, std::move(_config))
		{
		}
		~JitBlockEmitter()
		{
			std::free(m_retainedTextData);
		}

		void reset(JitConfig&& _config)
		{
			retainTextBuffer();
			codeHolder.reset(asmjit::ResetPolicy::kSoft);
			emitter.clearDiagnosticOptions(emitter.diagnosticOptions());
			emitter.clearEncodingOptions(emitter.encodingOptions());

			block.reset(std::move(_config));
		}

		asmjit::Error init(const asmjit::Environment& _environment)
		{
			auto err = codeHolder.init(_environment);
			if(err)
				return err;

			if(m_retainedTextData)
			{
				auto& buffer = codeHolder.sectionById(0)->buffer();
				buffer._data = m_retainedTextData;
				buffer._size = 0;
				buffer._capacity = m_retainedTextCapacity;
				buffer._flags = asmjit::CodeBufferFlags::kNone;
				m_retainedTextData = nullptr;
				m_retainedTextCapacity = 0;
			}

			return codeHolder.attach(&emitter);
		}

		const uint8_t* textBufferData() const
		{
			if(!codeHolder.isInitialized())
				return m_retainedTextData;
			return codeHolder.sectionById(0)->buffer().data();
		}

		size_t textBufferCapacity() const
		{
			if(!codeHolder.isInitialized())
				return m_retainedTextCapacity;
			return codeHolder.sectionById(0)->buffer().capacity();
		}

	private:
		void retainTextBuffer()
		{
			if(!codeHolder.isInitialized())
				return;

			auto& buffer = codeHolder.sectionById(0)->buffer();
			if(!buffer.data() || buffer.isExternal()
				|| buffer.capacity() > MaxRetainedTextBufferBytes)
				return;

			std::free(m_retainedTextData);
			m_retainedTextData = buffer._data;
			m_retainedTextCapacity = buffer._capacity;
			buffer._data = nullptr;
			buffer._size = 0;
			buffer._capacity = 0;
			buffer._flags = asmjit::CodeBufferFlags::kNone;
		}

		uint8_t* m_retainedTextData = nullptr;
		size_t m_retainedTextCapacity = 0;

	public:

		asmjit::CodeHolder codeHolder;
		JitEmitter emitter;
		JitBlock block;
	};
}

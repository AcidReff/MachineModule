#pragma once

#include <sstream>
#include <string>
#include <iomanip>

namespace Logging
{
	typedef void (*LogFunc)(const std::string&);

	void g_logToConsole( const std::string& _s );
	void g_logToFile( const std::string& _s );
	void setLogFunc(LogFunc _func);
}

#define LOGTOCONSOLE(ss)	{ Logging::g_logToConsole( (ss).str() ); }
#define LOGTOFILE(ss)		{ Logging::g_logToFile( (ss).str() ); }

#define LOG(S)																												\
do																															\
{																															\
	std::stringstream __ss__logging_h;	__ss__logging_h << __func__ << "@" << __LINE__ << ": " << S;						\
																															\
	LOGTOCONSOLE(__ss__logging_h)																							\
}																															\
while(false)

#define LOGF(S)																												\
do																															\
{																															\
	std::stringstream __ss__logging_h;	__ss__logging_h << S;																\
																															\
	LOGTOFILE(__ss__logging_h)																								\
}																															\
while (false)

#define LOGFMT(fmt, ...)	LOG(Logging::string_format(fmt,  ##__VA_ARGS__))

// Normal emulation/setup diagnostics may be reachable from realtime processing.
// Keep them compiled out unless a diagnostic build opts in explicitly.  The
// disabled form deliberately does not reference S, so stream operands are not
// evaluated and no stringstream is constructed.
#ifndef DSP56K_DIAGNOSTIC_LOGGING
#define DSP56K_DIAGNOSTIC_LOGGING 0
#endif

#if DSP56K_DIAGNOSTIC_LOGGING
#define LOG_DIAGNOSTIC(S)	LOG(S)
#else
#define LOG_DIAGNOSTIC(S)	do {} while(false)
#endif

#define HEX(S)			std::hex << std::setfill('0') << std::setw(6) << S
#define HEXN(S, n)		std::hex << std::setfill('0') << std::setw(n) << (uint32_t)S

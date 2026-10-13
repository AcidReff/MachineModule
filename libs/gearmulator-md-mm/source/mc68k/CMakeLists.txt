cmake_minimum_required(VERSION 3.16)

# Set the project name
project(68kEmu)

include(CTest)
  
# Add sub directories
add_library(68kEmu STATIC)

set(SOURCES_MUSASHI
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68k.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kconf.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kcpu.c
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kcpu.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kcfcycles.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kdasm.c
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kfpu.c
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kmmu.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kops.c
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/m68kops.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/softfloat/mamesf.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/softfloat/milieu.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/softfloat/softfloat.c
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/softfloat/softfloat.h
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/softfloat/softfloat-macros
	${CMAKE_CURRENT_SOURCE_DIR}/Musashi/softfloat/softfloat-specialize
)

set(SOURCES
	cpuState.h
	gpt.cpp gpt.h
	hdi08.cpp hdi08.h
	hdi08periph.h
	logging.cpp logging.h
	mc68k.cpp mc68k.h
	musashiEntry.h
	peripheralBase.cpp peripheralBase.h
	peripheralTypes.h
	port.cpp port.h
	qsm.cpp qsm.h
	qspi.cpp qspi.h
	sim.cpp sim.h
)

target_sources(68kEmu PRIVATE ${SOURCES} ${SOURCES_MUSASHI})
source_group("source" FILES ${SOURCES})
source_group("source\\Musashi" FILES ${SOURCES_MUSASHI})

set_property(TARGET 68kEmu PROPERTY CXX_STANDARD 17)

if(BUILD_TESTING)
	add_executable(mc68kColdFireTimingTest test/coldfire_timing_test.cpp)
	target_link_libraries(mc68kColdFireTimingTest PRIVATE 68kEmu)
	target_include_directories(mc68kColdFireTimingTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_features(mc68kColdFireTimingTest PRIVATE cxx_std_17)
	add_test(NAME mc68kColdFireTimingTest COMMAND mc68kColdFireTimingTest)
	set_tests_properties(mc68kColdFireTimingTest PROPERTIES LABELS "UnitTest")

	add_executable(mc68kHdi08ReceiveTest test/hdi08_receive_test.cpp)
	target_link_libraries(mc68kHdi08ReceiveTest PRIVATE 68kEmu)
	target_include_directories(mc68kHdi08ReceiveTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_features(mc68kHdi08ReceiveTest PRIVATE cxx_std_17)
	add_test(NAME mc68kHdi08ReceiveTest COMMAND mc68kHdi08ReceiveTest)
	set_tests_properties(mc68kHdi08ReceiveTest PROPERTIES LABELS "UnitTest")

	add_executable(mc68kColdFireDivideTest test/coldfire_div_test.cpp)
	target_link_libraries(mc68kColdFireDivideTest PRIVATE 68kEmu)
	target_include_directories(mc68kColdFireDivideTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
	target_compile_features(mc68kColdFireDivideTest PRIVATE cxx_std_17)
	add_test(NAME mc68kColdFireDivideTest COMMAND mc68kColdFireDivideTest)
	set_tests_properties(mc68kColdFireDivideTest PROPERTIES LABELS "UnitTest")

	# Integrations can require this fixture so focused CTest selections still
	# exercise the instruction and host-port contracts they depend on.
	set_tests_properties(mc68kColdFireTimingTest mc68kHdi08ReceiveTest
		mc68kColdFireDivideTest PROPERTIES FIXTURES_SETUP Mc68kCore)
endif()

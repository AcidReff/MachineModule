cmake_minimum_required(VERSION 3.10)
project(dsp56kBase)

add_library(dsp56kBase)

set(SOURCES
bitfield.h
buildconfig.h
conditionvariable.cpp conditionvariable.h
cowmemory.cpp cowmemory.h
dspassert.cpp dspassert.h
fastmath.h
logging.cpp logging.h
mmuarray.h
pagedarray.h
mmuhelper.cpp mmuhelper.h
mutex.cpp mutex.h
ringbuffer.h
semaphore.h
sharedaudiobuffer.h
sharedaudioreducer.h
threadtools.cpp threadtools.h
trigger.cpp trigger.h
tscprobe.h
win8sync.cpp win8sync.h
)

target_sources(dsp56kBase PRIVATE ${SOURCES})

# Diagnostic TSC probes in the JIT exec path (tscprobe.h). Off by default.
option(DSP56K_TSC_PROBES "Build diagnostic TSC probes into the DSP exec path" OFF)
if(DSP56K_TSC_PROBES)
	target_compile_definitions(dsp56kBase PUBLIC DSP56K_TSC_PROBES)
endif()
source_group("source" FILES ${SOURCES})

if(MSVC)
	SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /constexpr:steps10000000")
elseif(UNIX AND NOT APPLE)
	if(NOT ANDROID)
		SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fconstexpr-loop-limit=10000000")
	endif()
endif()

if(UNIX AND NOT APPLE)
	set(THREADS_PREFER_PTHREAD_FLAG ON)
	find_package(Threads REQUIRED)
	target_link_libraries(dsp56kBase PUBLIC Threads::Threads rt)
endif()

target_include_directories(dsp56kBase PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

add_executable(pagedArrayTest pagedarray_test.cpp)
target_link_libraries(pagedArrayTest PRIVATE dsp56kBase)
add_test(NAME pagedArrayTest COMMAND pagedArrayTest)

add_executable(cowMemoryTest cowmemory_test.cpp)
target_link_libraries(cowMemoryTest PRIVATE dsp56kBase)
add_test(NAME cowMemoryTest COMMAND cowMemoryTest)
set_tests_properties(cowMemoryTest PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 30)

add_executable(sharedAudioBufferTest sharedaudiobuffer_test.cpp)
target_link_libraries(sharedAudioBufferTest PRIVATE dsp56kBase)
add_test(NAME sharedAudioBufferTest COMMAND sharedAudioBufferTest)

add_executable(sharedAudioReducerTest sharedaudioreducer_test.cpp)
target_link_libraries(sharedAudioReducerTest PRIVATE dsp56kBase)
add_test(NAME sharedAudioReducerTest COMMAND sharedAudioReducerTest)

add_executable(ringBufferTest ringbuffer_test.cpp)
target_link_libraries(ringBufferTest PRIVATE dsp56kBase)
add_test(NAME ringBufferTest COMMAND ringBufferTest)

add_executable(semaphoreTest semaphore_test.cpp)
target_link_libraries(semaphoreTest PRIVATE dsp56kBase)
add_test(NAME semaphoreTest COMMAND semaphoreTest)

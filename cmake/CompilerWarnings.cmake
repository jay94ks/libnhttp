# Shared warning configuration, applied via the `nhttp_warnings` INTERFACE target.
# Every first-party target in this repo should link against `nhttp_warnings` (PRIVATE).
# Vendored third-party code under src/depends/ must NOT link against it.

add_library(nhttp_warnings INTERFACE)

if(MSVC)
	# MSVC's cl.exe doesn't understand GCC/Clang's -W flags at all (no 1:1
	# equivalents for -Wshadow/-Wold-style-cast/-Wconversion etc. either) —
	# /W4 + /permissive- (strict conformance) is the closest practical match.
	# /utf-8 is required, not cosmetic: this repo's UTF-8 source files have no
	# BOM, so without it cl.exe decodes them using the system's ANSI codepage
	# (e.g. CP949 on a Korean-locale Windows install) instead of UTF-8, and
	# every non-ASCII character (em-dashes, arrows, etc. throughout comments)
	# trips C4819 — fatal under /WX. Found while bringing up the first native
	# Windows build; see CLAUDE.md's Windows-support notes.
	# /wd4324 ("structure was padded due to alignment specifier") is a
	# deliberate, working-as-intended pattern in
	# include/nhttp/async/detail/mpmc_queue.hpp (PLAN.md's P4): its enqueue/
	# dequeue position counters are each `alignas(64)`'d specifically to land
	# on separate cache lines and avoid false sharing between producer and
	# consumer threads — the padding C4324 flags *is* the point, not an
	# accident to fix. GCC/Clang have no equivalent warning for this pattern.
	set(NHTTP_WARNING_FLAGS /W4 /permissive- /utf-8 /wd4324)

	if(NHTTP_WARNINGS_AS_ERRORS)
		list(APPEND NHTTP_WARNING_FLAGS /WX)
	endif()
else()
	set(NHTTP_WARNING_FLAGS
		-Wall
		-Wextra
		-Wpedantic
		-Wshadow
		-Wnon-virtual-dtor
		-Wold-style-cast
		-Wcast-align
		-Woverloaded-virtual
		-Wconversion
		-Wsign-conversion
		-Wnull-dereference
		-Wdouble-promotion
	)

	if(NHTTP_WARNINGS_AS_ERRORS)
		list(APPEND NHTTP_WARNING_FLAGS -Werror)
	endif()
endif()

target_compile_options(nhttp_warnings INTERFACE ${NHTTP_WARNING_FLAGS})

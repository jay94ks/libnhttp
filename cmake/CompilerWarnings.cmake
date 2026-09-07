# Shared warning configuration, applied via the `nhttp_warnings` INTERFACE target.
# Every first-party target in this repo should link against `nhttp_warnings` (PRIVATE).
# Vendored third-party code under src/depends/ must NOT link against it.

add_library(nhttp_warnings INTERFACE)

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

target_compile_options(nhttp_warnings INTERFACE ${NHTTP_WARNING_FLAGS})

cmake_minimum_required(VERSION 3.10)


project(
	f4ee-patched
	VERSION 0.1.0
	LANGUAGES CXX
)

find_path(JSON_INCLUDE_DIR "json/json.h")

set( F4SE_STATIC_LIB 1 )

if (NOT TARGET f4se)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../f4se/f4se f4se)	# bundled
endif()

if (NOT TARGET f4se_common)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../f4se/f4se_common f4se_common)	# bundled
endif()

if (NOT TARGET common)
	add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../common common)	# bundled
endif()

find_package(jsoncpp CONFIG REQUIRED)

include(cmake/headerlist.cmake)
include(cmake/sourcelist.cmake)

add_library(
	f4ee
	SHARED
	${headers}
	${sources}
)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
	${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc
	@ONLY
)

target_sources(
	"f4ee"
	PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/cmake/version.rc
)

target_include_directories(
	f4ee
	PRIVATE
		${CMAKE_CURRENT_SOURCE_DIR}/include
		${JSON_INCLUDE_DIR}
)

target_compile_definitions(
	f4ee
	PRIVATE
		RUNTIME
)

target_link_libraries(
	f4ee
	PRIVATE
		f4se::f4se
		f4se::f4se_common
		xse::common
		JsonCpp::JsonCpp
		shlwapi
)



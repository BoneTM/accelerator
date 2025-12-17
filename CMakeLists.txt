cmake_minimum_required(VERSION 3.25)
project(Accelerator CXX ASM C)

include(cmake/CPM.cmake)

CPMAddPackage(NAME fmt
              GITHUB_REPOSITORY fmtlib/fmt
              GIT_TAG 11.1.4
              OPTIONS "FMT_INSTALL OFF")
			  
CPMAddPackage(NAME breakpad
              GITHUB_REPOSITORY Nukoooo/breakpad
              GIT_TAG fa1a48c78383f09226ccd38b552e17b782f9d3ef
              SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/breakpad/)

set(accelerator_SOURCES
	"src/accelerator.cpp"
)

set(minidump_processor_SOURCES
	"src/minidump_processor.cpp"
)

add_library(accelerator SHARED)

target_sources(accelerator PRIVATE ${accelerator_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${accelerator_SOURCES})

target_compile_features(accelerator PUBLIC
	cxx_std_20
)
target_compile_definitions(accelerator PRIVATE _GLIBCXX_USE_CXX11_ABI=0)

target_include_directories(accelerator PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/includes
	${breakpad_SOURCE_DIR}/src/
)

target_link_libraries(accelerator PUBLIC
	breakpad
	fmt::fmt-header-only
)

target_link_options(accelerator PUBLIC
    -static-libstdc++
    -static-libgcc
)

add_executable(minidump_processor)

target_sources(minidump_processor PRIVATE ${minidump_processor_SOURCES})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${minidump_processor_SOURCES})

target_compile_features(minidump_processor PUBLIC
	cxx_std_20
)
target_compile_definitions(minidump_processor PRIVATE _GLIBCXX_USE_CXX11_ABI=0)

target_include_directories(minidump_processor PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/includes
	${breakpad_SOURCE_DIR}/src/
)

target_link_libraries(minidump_processor PUBLIC
	breakpad
	fmt::fmt-header-only
)

target_link_options(minidump_processor PUBLIC
    -static-libstdc++
    -static-libgcc
)

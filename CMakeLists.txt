cmake_minimum_required(VERSION 3.15)
project(jtjson)

include(GNUInstallDirs)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "${DKP_INSTALL_PREFIX_INIT}" CACHE PATH "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")

# Main JSON library
add_library(jtjson STATIC
    json.cpp
    double-conversion/bignum.cc
    double-conversion/bignum-dtoa.cc
    double-conversion/cached-powers.cc
    double-conversion/double-to-string.cc
    double-conversion/fast-dtoa.cc
    double-conversion/fixed-dtoa.cc
    double-conversion/string-to-double.cc
    double-conversion/strtod.cc
)

target_include_directories(jtjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Install the library
install(
	TARGETS jtjson
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# Install headers
install(
    FILES ${PROJECT_SOURCE_DIR}/json.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    RENAME jtjson.h
)

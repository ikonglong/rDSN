cmake_minimum_required(VERSION 2.8.7)

#set(DSN_DEBUG_CMAKE TRUE)

#cmake_policy(SET CMP0048 NEW)
#project(dsn VERSION 1.0 LANGUAGES C CXX)
project(dsn C CXX)

set(DSN_BUILD_RUNTIME TRUE)

message(STATUS "use git source ${DSN_GIT_SOURCE}")
message(STATUS "use BUILD_PLUGINS = ${BUILD_PLUGINS}")
message(STATUS "use BOOST_INCLUDEDIR = ${BOOST_INCLUDEDIR}")


if((NOT DEFINED DSN_GIT_SOURCE) OR (DSN_GIT_SOURCE STREQUAL ""))
    set(DSN_GIT_SOURCE "github")
endif()

set(GIT_SOURCE_FILE "ext/gitsource.${DSN_GIT_SOURCE}")

include(bin/dsn.cmake)

if(DEFINED WARNING_ALL)
    #add_compile_options("-Wall")
    SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wall")
    SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wall")
endif()
OPTION(ENABLE_GCOV "Enable gcov (debug, Linux builds only)" OFF)
if(ENABLE_GCOV AND UNIX)
    #add_compile_options("-fprofile-arcs" "-ftest-coverage")
    SET(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -fprofile-arcs -ftest-coverage -DENABLE_GCOV")
    SET(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -fprofile-arcs -ftest-coverage -DENABLE_GCOV")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fprofile-arcs -ftest-coverage -lgcov")
endif()

dsn_add_pseudo_projects()

configure_file("bin/config.onecluster.ini.template" "${CMAKE_CURRENT_SOURCE_DIR}/bin/config.onecluster.ini")

include(${GIT_SOURCE_FILE})
add_subdirectory(ext/thrift)
add_subdirectory(ext/gtest)
add_subdirectory(ext/protobuf)
#
# !!! gflags is currently not used, and when it is enabled, it causes failure on windows compilation
# add_subdirectory(ext/gflags)
#

add_subdirectory(src)






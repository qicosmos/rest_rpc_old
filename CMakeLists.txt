cmake_minimum_required(VERSION 2.8)

project(rest_rpc)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++14")

if (CMAKE_BUILD_TYPE STREQUAL Debug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DEBUG")
    #add_definitions(
    #    -D_DEBUG
    #)
endif ()

find_package(Boost REQUIRED)
include_directories(${Boost_INCLUDE_DIRS})
SET(EXTRA_LIBS ${EXTRA_LIBS} ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})

include_directories(${PROJECT_SOURCE_DIR})

set(SOURCE_FILES 
	main.cpp
	)


add_executable(rest_rpc ${SOURCE_FILES})
target_link_libraries(rest_rpc ${EXTRA_LIBS})

include (InstallRequiredSystemLibraries)
set (CPACK_PACKAGE_VERSION_MAJOR "1")
set (CPACK_PACKAGE_VERSION_MINOR "0")
SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "David Doria")
include (CPack)
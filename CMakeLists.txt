cmake_minimum_required(VERSION 3.6)

project(SimpleCam
	DESCRIPTION "A small and documented example application for libcamera"
	LANGUAGES CXX)

set (CMAKE_CXX_STANDARD 17)

set (CMAKE_CXX_FLAGS "-Wall -Winvalid-pch -Wnon-virtual-dtor -Wextra -Werror -Wno-unused-parameter")

option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

find_package(PkgConfig)

pkg_check_modules(LIBCAMERA REQUIRED IMPORTED_TARGET libcamera)
message(STATUS "libcamera library found:")
message(STATUS "    version: ${LIBCAMERA_VERSION}")
message(STATUS "    libraries: ${LIBCAMERA_LINK_LIBRARIES}")
message(STATUS "    include path: ${LIBCAMERA_INCLUDE_DIRS}")

# libevent is used specifically by simple-cam as its event loop.
# Applications may use a different event handling implementation.
pkg_check_modules(LIBEVENT REQUIRED IMPORTED_TARGET libevent_pthreads)
message(STATUS "libevent_pthreads library found:")
message(STATUS "    version: ${LIBEVENT_VERSION}")
message(STATUS "    libraries: ${LIBEVENT_LINK_LIBRARIES}")
message(STATUS "    include path: ${LIBEVENT_INCLUDE_DIRS}")

pkg_check_modules(X11 QUIET x11)
message(STATUS "X11 library found:")
message(STATUS "    version: ${X11_VERSION}")
message(STATUS "    libraries: ${X11_LINK_LIBRARIES}")
message(STATUS "    include path: ${X11_INCLUDE_DIRS}")

pkg_check_modules(EPOXY QUIET epoxy)
message(STATUS "epoxy library found:")
message(STATUS "    version: ${EPOXY_VERSION}")
message(STATUS "    libraries: ${EPOXY_LINK_LIBRARIES}")
message(STATUS "    include path: ${EPOXY_INCLUDE_DIRS}")

pkg_check_modules(LIBDRM QUIET libdrm)
message(STATUS "LIBDRM_LINK_LIBRARIES=${LIBDRM_LINK_LIBRARIES}")
message(STATUS "LIBDRM display mode enabled")

include_directories(${CMAKE_SOURCE_DIR} ${LIBCAMERA_INCLUDE_DIRS} ${LIBEVENT_INCLUDE_DIRS} ${LIBDRM_INCLUDE_DIRS}) 
set(TARGET_LIBS ${TARGET_LIBS} ${X11_LIBRARIES} ${EPOXY_LIBRARIES} ${LIBDRM_LIBRARIES})

add_executable(simple-cam event_loop.cpp eglUtil.cpp mesh.cpp simple-cam.cpp) 

target_link_libraries(simple-cam PkgConfig::LIBEVENT)
target_link_libraries(simple-cam PkgConfig::LIBCAMERA)
target_link_libraries(simple-cam ${TARGET_LIBS})


cmake_minimum_required(VERSION 3.0)
project (runlxc)

set(PROJECT_PRETTY_NAME "Run LXC container")
set(PROJECT_MAJOR_VERSION "1.0")
set(PROJECT_MINOR_VERSION "1")
set(PROJECT_VERSION "${PROJECT_MAJOR_VERSION}.${PROJECT_MINOR_VERSION}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(GIO REQUIRED gio-2.0)
pkg_check_modules(ILMCONTROL REQUIRED ilmControl)
pkg_check_modules(ILMINPUT REQUIRED ilmInput)
pkg_check_modules(LXC REQUIRED lxc)

# No configuration
# configure_file (
#   "${PROJECT_SOURCE_DIR}/config.h.in"
#   "${PROJECT_BINARY_DIR}/config.h"
# )
#
# include_directories("${PROJECT_BINARY_DIR}")

include_directories(
  "include"
  "${ILMCONTROL_INCLUDE_DIRS}"
  "${ILMINPUT_INCLUDE_DIRS}"
  "${GLIB_INCLUDE_DIRS}"
  "${GIO_INCLUDE_DIRS}"
  )

SET(SRC_FILES
    src/runlxc.cpp
    src/ilm_control.cpp
)

SET(LIBRARIES
  ${ILMCONTROL_LIBRARIES}
  ${ILMINPUT_LIBRARIES}
  lxc
  ${GLIB_LIBRARIES}
  ${GIO_LIBRARIES}
  pthread
  )

add_executable (runlxc ${SRC_FILES})

TARGET_LINK_LIBRARIES (runlxc ${LIBRARIES})

install (TARGETS runlxc DESTINATION bin)

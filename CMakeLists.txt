cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(act)

SET(ACT_INSTALL_PREFIX "/usr/local" CACHE PATH "Prefix prepended to install directories")
SET(CMAKE_INSTALL_PREFIX "${ACT_INSTALL_PREFIX}" CACHE INTERNAL "Prefix prepended to install directories" FORCE)
SET(UDEV_INSTALL_DIR "/etc/udev/rules.d")

add_subdirectory(drivers)
add_subdirectory(utils)
add_subdirectory(libs)
add_subdirectory(softw)


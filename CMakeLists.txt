cmake_minimum_required(VERSION 2.8 FATAL_ERROR)
project(act)

set(ACT_INSTALL_PREFIX "/usr/local" CACHE PATH "Prefix prepended to install directories")
set(CMAKE_INSTALL_PREFIX "${ACT_INSTALL_PREFIX}" CACHE INTERNAL "Prefix prepended to install directories" FORCE)

add_subdirectory(drivers)
# add_subdirectory(utils)
# add_subdirectory(libs)
# add_subdirectory(softw)

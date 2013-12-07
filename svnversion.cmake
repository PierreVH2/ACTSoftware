# From http://stackoverflow.com/questions/3780667/use-cmake-to-get-build-time-svn-revision
# the FindSubversion.cmake module is part of the standard distribution
include(FindSubversion)
# extract working copy information for SOURCE_DIR into ACT_SVN_XXX variables
Subversion_WC_INFO(${SOURCE_DIR} ACT_SVN)
# write a file with the MINOR_VER define
file(WRITE svnversion.h.txt "#define MINOR_VER ${ACT_SVN_WC_REVISION}\n")
# copy the file to the final header only if the version changes
# reduces needless rebuilds
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different svnversion.h.txt svnversion.h)
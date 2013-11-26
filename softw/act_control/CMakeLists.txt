FIND_PACKAGE(GTK2 REQUIRED)
FIND_PACKAGE(PkgConfig REQUIRED)

PKG_CHECK_MODULES(ARGTABLE REQUIRED argtable2)
LINK_DIRECTORIES(${ARGTABLE_LIBRARY_DIRS})
INCLUDE_DIRECTORIES(${ARGTABLE_INCLUDE_DIRS})

INCLUDE_DIRECTORIES(${ACT_LIB_SRC})
INCLUDE_DIRECTORIES(${GTK2_INCLUDE_DIRS})
SET(CNTRL_SOURCE_FILES act_control.c act_control_config.c act_control_config.h net_basic.c net_basic.h net_dataccd.c net_dataccd.h net_datapmt.c net_datapmt.h net_genl.c net_genl.h net_targset.c net_targset.h subprogrammes.c subprogrammes.h)
ADD_EXECUTABLE(act_control ${CNTRL_SOURCE_FILES} ${ACT_LIB_SRC}/act_ipc.h ${ACT_LIB_SRC}/act_timecoord.h ${ACT_LIB_SRC}/act_log.h)
TARGET_LINK_LIBRARIES(act_control ${GTK2_LIBRARIES} ${ARGTABLE_LIBRARIES} mysqlclient act_timecoord act_log)
INSTALL(TARGETS act_control RUNTIME DESTINATION bin)

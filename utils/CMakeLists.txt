FIND_PACKAGE(GTK)
INCLUDE_DIRECTORIES(${ACT_DRV_SRC}/merlin_driver)
INCLUDE_DIRECTORIES(${ACT_DRV_SRC}/motor_driver)
INCLUDE_DIRECTORIES(${ACT_DRV_SRC}/act_plc)
INCLUDE_DIRECTORIES(${ACT_DRV_SRC}/plc_ldisc)
INCLUDE_DIRECTORIES(${ACT_DRV_SRC}/pmt_driver)
INCLUDE_DIRECTORIES(${ACT_DRV_SRC}/time_driver)
INCLUDE_DIRECTORIES(${ACT_LIB_SRC})

ADD_EXECUTABLE(merlin_driver_test merlin_driver_test.c ${ACT_DRV_SRC}/merlin_driver/merlin_driver.h ${ACT_DRV_SRC}/merlin_driver/ccd_defs.h)
TARGET_LINK_LIBRARIES(merlin_driver_test m)

# This programme is for the MERLIN acquisition system simulator, but it's very outdated and needs to be updated to conform to the new merlin driver interface and the new IPC spec
# IF (DEFINED MERLIN_SIM)
#   ADD_DEFINITIONS(-DMERLIN_SIM)
#   ADD_EXECUTABLE(merlin_sim merlin_sim.c ${ACT_DRV_SRC}/merlin_driver/merlin_driver.h ${ACT_DRV_SRC}/merlin_driver/ccd_defs.h ${ACT_LIB_SRC}/act_ipc.h)
# ENDIF (DEFINED MERLIN_SIM) 

ADD_EXECUTABLE(merlin_ztest merlin_ztest.c)

IF (DEFINED MOTOR_SIM)
  ADD_DEFINITIONS(-DMOTOR_SIM)
  ADD_EXECUTABLE(motor_sim motor_sim.c ${ACT_DRV_SRC}/motor_driver/motor_driver.h)
  INSTALL(
      TARGETS motor_sim
      PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_WRITE GROUP_READ
      RUNTIME DESTINATION bin
  )
ENDIF (DEFINED MOTOR_SIM)

ADD_EXECUTABLE(plc_hog plc_hog.c ${ACT_DRV_SRC}/plc_ldisc/plc_ldisc.h)

IF (DEFINED PLC_SIM)
  ADD_DEFINITIONS(-DPLC_SIM)
  ADD_EXECUTABLE(plc_sim plc_sim.c ${ACT_DRV_SRC}/act_plc/plc_definitions.h ${ACT_DRV_SRC}/act_plc/act_plc.h)
  INSTALL(
      TARGETS plc_sim
      PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_WRITE GROUP_READ
      RUNTIME DESTINATION bin
  )
ENDIF (DEFINED PLC_SIM)

ADD_EXECUTABLE(pmt_driver_test pmt_driver_test.c ${ACT_DRV_SRC}/pmt_driver/pmt_driver.h)

ADD_EXECUTABLE(pmt_set_channel pmt_set_channel.c ${ACT_DRV_SRC}/pmt_driver/pmt_driver.h)

ADD_EXECUTABLE(pmt_set_overillum_rate pmt_set_overillum_rate.c ${ACT_DRV_SRC}/pmt_driver/pmt_driver.h)

ADD_EXECUTABLE(pmt_test pmt_test.c)

ADD_EXECUTABLE(time_resync time_resync.c ${ACT_DRV_SRC}/time_driver/time_driver.h)

# Utilities for privileged user only
INSTALL(TARGETS merlin_driver_test merlin_ztest pmt_driver_test pmt_set_overillum_rate pmt_set_channel pmt_driver_test pmt_driver_test
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_WRITE GROUP_READ
        RUNTIME DESTINATION bin
)

# Utilities for everyone
INSTALL(
    TARGETS time_resync plc_hog
    PERMISSIONS WORLD_EXECUTE WORLD_WRITE WORLD_READ
    RUNTIME DESTINATION bin
)

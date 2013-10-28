set(MODULE_HEADER_FILES     merlin_driver/merlin_driver.h merlin_driver/ccd_defs.h
                            pmt_driver/pmt_driver.h pmt_driver/pmt_defs.h
                            time_driver/time_driver.h time_driver/parapin.h
                            plc_ldisc/plc_ldisc.h
                            act_plc/act_plc.h act_plc/plc_definitions.h
                            motor_driver/motor_intfce.h motor_driver/motor_defs.h motor_driver/motor_funcs.h motor_driver/motor_driver.h)

set(MODULE_SOURCE_FILES     merlin_driver/merlin_driver.c
                            pmt_driver/pmt_driver.c
                            time_driver/time_driver.c
                            plc_ldisc/plc_ldisc.c
                            act_plc/act_plc.c
                            motor_driver/motor_funcs.c
                            motor_driver/motor_driver.c
                            motor_driver/motor_intfce.c)


set(MODULE_DEPENDS          ${MODULE_HEADER_FILES} ${MODULE_SOURCE_FILES})


set(MOTOR_DRIVER_INPUT_FILES   motor_driver/motor_driver.c
                               motor_driver/motor_driver.h
                               motor_driver/motor_intfce.c
                               motor_driver/motor_intfce.h
                               motor_driver/motor_defs.h)

set(MOTOR_DRIVER_OUTPUT_FILES  motor_driver.mod.o
                               .motor_driver.ko.cmd
                               .motor_driver.o.cmd
                               .motor_driver.mod.o.cmd
                               motor_driver/motor_funcs.o
                               motor_driver/.motor_funcs.o.cmd
                               motor_driver/.motor_intfce.o.cmd
                               motor_driver/motor_intfce.o
                               motor_driver/.motor_driver.o.cmd
                               motor_driver/motor_driver.o
                               motor_driver.mod.c
                               motor_driver.ko
                               motor_driver.o)


set(ACT_PLC_INPUT_FILES       act_plc/act_plc.c
                              act_plc/act_plc.h
                              act_plc/plc_definitions.h)

set(ACT_PLC_OUTPUT_FILES       .act_plc.ko.cmd
                               act_plc.mod.c
                               act_plc.o
                               .act_plc.mod.o.cmd
                               act_plc.mod.o
                               act_plc/.act_plc.ko.cmd
                               act_plc/act_plc.mod.c
                               act_plc/act_plc.o
                               act_plc/.act_plc.mod.o.cmd
                               act_plc/modules.order
                               act_plc/Module.symvers
                               act_plc/.tmp_versions
                               act_plc/act_plc.mod.o
                               act_plc/act_plc.ko
                               act_plc/.act_plc.o.cmd
                               act_plc.ko
                               .act_plc.o.cmd)


set(MERLIN_DRIVER_INPUT_FILES  merlin_driver/merlin_driver.c
                               merlin_driver/merlin_driver.h
                               merlin_driver/ccd_defs.h)

set(MERLIN_DRIVER_OUTPUT_FILES merlin_driver.mod.o
                               merlin_driver.ko
                               merlin_driver.o
                               merlin_driver/merlin_driver.o
                               merlin_driver/.merlin_driver.o.cmd
                               merlin_driver.mod.c
                               .merlin_driver.mod.o.cmd
                               .merlin_driver.o.cmd
                               .merlin_driver.ko.cmd)


set(PLC_LDISC_INPUT_FILES      plc_ldisc/plc_ldisc.c
                               plc_ldisc/plc_ldisc.h)                               

set(PLC_LDISC_OUTPUT_FILES     .plc_ldisc.ko.cmd
                               plc_ldisc.mod.c
                               .plc_ldisc.o.cmd
                               plc_ldisc/.plc_ldisc.o.cmd
                               plc_ldisc/plc_ldisc.o
                               plc_ldisc.o
                               .plc_ldisc.mod.o.cmd
                               plc_ldisc.mod.o
                               plc_ldisc.ko)


set(PMT_DRIVER_INPUT_FILES     pmt_driver/pmt_driver.c
                               pmt_driver/pmt_driver.h
                               pmt_driver/pmt_defs.h)

set(PMT_DRIVER_OUTPUT_FILES    pmt_driver.o
                               .pmt_driver.o.cmd
                               pmt_driver.mod.c
                               pmt_driver/.pmt_driver.o.cmd
                               pmt_driver/pmt_driver.o
                               .pmt_driver.ko.cmd
                               pmt_driver.ko
                               pmt_driver.mod.o
                               .pmt_driver.mod.o.cmd)


set(TIME_DRIVER_INPUT_FILES    time_driver/time_driver.c
                               time_driver/time_driver.h)

set(TIME_DRIVER_OUTPUT_FILES   time_driver.mod.c
                               .time_driver.o.cmd
                               time_driver.mod.o
                               time_driver/.time_driver.o.cmd
                               time_driver/time_driver.o
                               time_driver.o
                               .time_driver.ko.cmd
                               .time_driver.mod.o.cmd
                               time_driver.ko)

set(MISC_OUTPUT_FILES          modules.order
                               Module.symvers
                               .tmp_versions
                               .tmp_versions/time_driver.mod
                               .tmp_versions/act_plc.mod
                               .tmp_versions/pmt_driver.mod
                               .tmp_versions/merlin_driver.mod
                               .tmp_versions/plc_ldisc.mod
                               .tmp_versions/motor_driver.mod)

set(MODULE_OUTPUT_FILES        ${MOTOR_DRIVER_OUTPUT_FILES}
                               ${ACT_PLC_OUTPUT_FILES}
                               ${MERLIN_DRIVER_OUTPUT_FILES}
                               ${PLC_LDISC_OUTPUT_FILES}
                               ${PMT_DRIVER_OUTPUT_FILES}
                               ${TIME_DRIVER_OUTPUT_FILES}
                               ${MISC_OUTPUT_FILES})

set(DRIVER_FILES               merlin_driver.ko pmt_driver.ko time_driver.ko plc_ldisc.ko act_plc.ko motor_driver.ko)
set(DRIVER_TARGET_NAME         act_drivers)
set(DRIVER_BIN_DIR             ${CMAKE_BINARY_DIR}/drivers)
set(MODULE_SOURCE_DIR          ${PROJECT_SOURCE_DIR}/drivers)
set(MODULE_BUILD_DIR           ${DRIVER_BIN_DIR}/src)
set(KERNEL_DIR                 "/lib/modules/${CMAKE_SYSTEM_VERSION}/build")
set(KBUILD_CMD                 ${CMAKE_MAKE_PROGRAM} -C ${KERNEL_DIR} M=${MODULE_BUILD_DIR} modules)
set(KBUILD_INSTALL_CMD         ${CMAKE_MAKE_PROGRAM} -C ${KERNEL_DIR} M=${MODULE_BUILD_DIR} modules_install)

file(COPY ${MOTOR_DRIVER_INPUT_FILES} DESTINATION ${MODULE_BUILD_DIR}/motor_driver/)
file(COPY ${ACT_PLC_INPUT_FILES} DESTINATION ${MODULE_BUILD_DIR}/act_plc/)
file(COPY ${PLC_LDISC_INPUT_FILES} DESTINATION ${MODULE_BUILD_DIR}/plc_ldisc/)
file(COPY ${MERLIN_DRIVER_INPUT_FILES} DESTINATION ${MODULE_BUILD_DIR}/merlin_driver/)
file(COPY ${PMT_DRIVER_INPUT_FILES} DESTINATION ${MODULE_BUILD_DIR}/pmt_driver/)
file(COPY ${MOTOR_DRIVER_INPUT_FILES} DESTINATION ${MODULE_BUILD_DIR}/time_driver/)
file(COPY "${MODULE_SOURCE_DIR}/Makefile" DESTINATION ${MODULE_BUILD_DIR})

add_custom_command (OUTPUT  ${DRIVER_FILES}
                    COMMAND ${KBUILD_CMD}
                    DEPENDS ${MODULE_DEPENDS} ${MODULE_SOURCE_DIR}/${MODULE_DEPENDS} VERBATIM)

add_custom_target (${DRIVER_TARGET_NAME}
                   ALL
                   DEPENDS ${DRIVER_FILES})

# add_custom_target (${MODULE_BUILD_DIR}/${MODULE_DEPENDS}
#                    ALL
#                    DEPENDS ${MODULE_SOURCE_DIR}/${MODULE_DEPENDS})

install(FILES ${MODULE_HEADER_FILES} DESTINATION include)
install(CODE "EXECUTE_PROCESS(COMMAND ${KBUILD_INSTALL_CMD})")

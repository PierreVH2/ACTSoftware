#! /bin/sh
# /etc/init.d/act_drivers
#

### BEGIN INIT INFO
# Provides:          act_drivers
# Required-Start:    $local_fs $network $named $time
# Required-Stop:     $local_fs
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description:  Load ACT drivers
# Description:       Loads all drivers of the ACT control software suite, synchronises to the time service and creates the appropriate device nodes.
### END INIT INFO

drivers_start ()
{
    echo "Loading ACT drivers"
    modprobe plc_ldisc
    modprobe act_plc
    modprobe motor_driver
    modprobe merlin_driver
    # echo "Testing for PLC"
    # nohup plc_hog $(find /dev \( -name ttyS* -o -name ttyUSB* \) ) &
    echo "Done"
}

drivers_stop ()
{
  killall -s SIGINT plc_hog
  if lsmod | grep -q ^merlin_driver ; then rmmod merlin_driver ; fi
  if lsmod | grep -q ^motor_driver ; then rmmod motor_driver ; fi
  if lsmod | grep -q ^act_plc ; then rmmod act_plc ; fi
  if lsmod | grep -q ^plc_ldisc ; then rmmod plc_ldisc ; fi
}

drivers_force_stop ()
{
  killall plc_hog
  rmmod -f merlin_driver
  rmmod -f motor_driver
  rmmod -f act_plc
  rmmod -f plc_ldisc
}

case "$1" in
  start)
    drivers_start
    ;;
  stop)
    drivers_stop
    ;;
  restart)
    drivers_stop
    drivers_start
    ;;
  force-reload)
    drivers_force_stop
    drivers_start
    ;;
  status)
    success=0
    if [ ! -c /dev/act_plc ] ;
    then
      echo "/dev/act_plc missing"
      success=1
    fi
    if [ ! -c /dev/act_motors ] ;
    then
      echo "/dev/act_motors missing"
      success=1
    fi
    if [ ! -c /dev/act_merlin ] ;
    then
      echo "/dev/act_merlin missing"
      success=1
    fi
    if [ ! `lsmod | grep -q ^merlin_driver` ] ;
    then
      echo "merlin_driver not loaded"
      success=1
    fi
    if [ ! `lsmod | grep -q ^motor_driver` ] ;
    then
      echo "motor_driver not loaded"
      success=1
    fi
    if [ ! `lsmod | grep -q ^act_plc` ] ;
    then
      echo "act_plc not loaded"
      success=1
    fi
    if [ $success = 0 ] ; then echo "All ACT drivers loaded and ready." ; fi
    ;;
  *)
    echo "Usage: /etc/init.d/act_drivers {start|stop}"
    exit 1
    ;;
esac

exit 0


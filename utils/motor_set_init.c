#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <motor_driver.h>

int main(int argc, char ** argv)
{
  int ret = 0, cmd = -1;
  unsigned char mask;
  if (argc != 2)
    ret = 1;
  else if (sscanf(argv[1], "%d", &cmd) != 2)
    ret = 1;
  else
  {
    switch (cmd)
    {
      case 0:
        mask = 0;
        break;
      case 1:
        mask = MOTOR_STAT_HA_INIT;
        break;
      case 2:
        mask = MOTOR_STAT_DEC_INIT;
        break;
      case 3:
        mask = MOTOR_STAT_HA_INIT | MOTOR_STAT_DEC_INIT;
        break;
      default:
        ret = 1;
    }
  }
  if (ret != 0)
  {
    fprintf(stderr, "Invalid usage.\n\nUse as follows:\n\t%s <init bitmask>. Where <init bitmask> is one of the following:\n" \
                    "\t0 - HA, Dec NOT initialised\n" \
                    "\t1 - HA initialised, Dec NOT initialised\n" \
                    "\t2 - HA NOT initialised, Dec initialised\n" \
                    "\t3 - HA, Dec both initialised\n" \
                    "WARNING: This programme is used to reset the coordinate system of the telescope and is dangerous if used incorrectly.\n",
           argv[0]);
    return 2;
  }
  
  int motor_fd = open("/dev/" MOTOR_DEVICE_NAME, O_RDWR | O_NONBLOCK);
  if (motor_fd < 0)
  {
    fprintf(stderr, "Failed to open motor device (/dev/%s) - %s\n\nAborted.\n", MOTOR_DEVICE_NAME, strerror(errno));
    return 1;
  }
  ret = ioctl(motor_fd, IOCTL_MOTOR_SET_INIT, mask);
  if (ret < 0)
  {
    fprintf(stderr, "Failed to set motor initialisation mask - %s.\nWARNING: Motor coordinates may be partially set. Proceed with extreme caution - preferably initialise the telescope with the hand paddle in the dome.\n", strerror(errno));
    close(motor_fd);
    return 1;
  }
  close(motor_fd);
  return 0;
}

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <motor_driver.h>
#include <motor_defs.h>

int main (int argc, char **argv)
{
  if (argc != 2)
  {
    fprintf(stderr, "Invalid usage.\nUse \"%s <track_rate>\" (where <track_rate> is the desired track rate in motor rate units).\nPlease remember that tracking must already have been activated by another programme before tracking can be tweaked using this utility.\n", argv[0]);
    return 2;
  }
  unsigned int track_rate;
  sscanf(argv[1], "%u", &track_rate);
  if ((track_rate < SLEW_RATE) || (track_rate > 2*SID_RATE))
  {
    fprintf(stderr, "Invalid track rate specified. Value must be between %u and %u.\nExiting.\n", SLEW_RATE, 2*SID_RATE);
    return 1;
  }
  int fd = open("/dev/" MOTOR_DEVICE_NAME, 0);
  if (fd <= 0)
  {
    fprintf(stderr, "Failed to open motor device driver character device (%s) - %s.\nExiting.\n", MOTOR_DEVICE_NAME, strerror(errno));
    return 1;
  }
  long ret = ioctl(fd, IOCTL_TEL_SET_TRACKING, &track_rate);
  if (ret != 0)
  {
    fprintf(stderr, "Error setting new track rate - %s\n", strerror(errno));
    ret = 1;
  }
  else
    printf("Done.\n");
  close(fd);
  return 0;
}

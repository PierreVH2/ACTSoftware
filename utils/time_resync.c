#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time_driver.h>
#include <sys/ioctl.h>

int main(void)
{
  int time_fd = open("/dev/" TIME_DEVICE_NAME, O_RDWR);
  if (time_fd <= 0)
  {
    fprintf(stderr, "Could not open time device driver character device /dev/%s - %s.\n", TIME_DEVICE_NAME, strerror(abs(time_fd)));
    return 1;
  }
  int ret = ioctl(time_fd, IOCTL_TIME_RESYNC, 0);
  if (ret != 0)
  {
    fprintf(stderr, "Failed to reset time synchronisation - %s.\n", strerror(abs(ret)));
    ret = 1;
  }
  else
  {
    printf("Success.\n");
    ret = 0;
  }
  close(time_fd);
  return ret;
}

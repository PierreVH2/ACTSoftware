#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <pmt_driver.h>

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    fprintf(stderr, "Invalid usage - specify new overillumination rate to use (and only that) on the command line.\n");
    return 2;
  }
  long overillum_rate = atoi(argv[1]);
  printf("Attempting to set overillumination rate %ld.\n", overillum_rate);
  
  int pmt_fd = open("/dev/" PMT_DEVICE_NAME, O_RDWR);
  if (pmt_fd <= 0)
  {
    fprintf(stderr, "Could not open PMT device driver character device /dev/%s - %s.\n", PMT_DEVICE_NAME, strerror(abs(pmt_fd)));
    return 1;
  }
  int ret = ioctl(pmt_fd, IOCTL_SET_OVERILLUM_RATE, &overillum_rate);
  if (ret < 0)
  {
    fprintf(stderr,"Error setting channel - %s.\n", strerror(abs(ret)));
    ret = 1;
  }
  else
  {
    printf("Success.\n");
    ret = 0;
  }
  close(pmt_fd);
  return ret;
}


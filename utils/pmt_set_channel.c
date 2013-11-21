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
    fprintf(stderr, "Invalid usage - specify counter number to use (and only that) on the command line.\n");
    return 2;
  }
  long counternum = atoi(argv[1]);
  printf("Attempting to select counter %ld.\n", counternum);

  int pmt_fd = open("/dev/" PMT_DEVICE_NAME, O_RDWR);
  if (pmt_fd <= 0)
  {
    fprintf(stderr, "Could not open PMT device driver character device /dev/%s - %s.\n", PMT_DEVICE_NAME, strerror(abs(pmt_fd)));
    return 1;
  }
  int ret = ioctl(pmt_fd, IOCTL_SET_CHANNEL, &counternum);
  if (ret < 0)
  {
    fprintf(stderr,"Error setting channel - %s.\n", strerror(abs(ret)));
    close(pmt_fd);
    return 1;
  }
  printf("Success.\n");
  close(pmt_fd);
  return 0;
}

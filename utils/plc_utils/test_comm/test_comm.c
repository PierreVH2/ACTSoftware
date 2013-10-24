#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <act_plc/act_plc.h>

int G_plc_fd = 0;

int get_plc_stat(int plc_fd, struct plc_status *plc_stat)
{
  return ioctl(plc_fd, (unsigned int)IOCTL_GET_STATUS, plc_stat);
}

void sigint_handler(int signum)
{
  if (signum != SIGINT)
    return;
  printf("Exiting.\n");
  close (G_plc_fd);
  exit(0);
}

int main()
{
  G_plc_fd = open(PLC_DEVICE_FILENAME, 0);
  if (G_plc_fd < 0)
  {
    fprintf(stderr, "Error opening PLC device %s\n", PLC_DEVICE_FILENAME);
    return 2;
  }
  printf("PLC device open. Starting infinite loop. Press Ctrl+C to exit gracefully.\n");

  struct plc_status stat;
  signal(SIGINT, &sigint_handler);
  sleep(5);
  unsigned char plc_comm_ok = 1;
  while (plc_comm_ok)
  {
    sleep(1);
    read(G_plc_fd, &plc_comm_ok, sizeof(plc_comm_ok));
    printf("PLC COMM: %s\n", plc_comm_ok ? "GOOD" : "FAIL");
    if (get_plc_stat(G_plc_fd, &stat) != 0)
      fprintf(stderr, "Error retrieving status from PLC.\n");
    else
      printf("Status received from PLC.\n");
  }

  printf("Going to empty loop\n");
  for (;;)
    sleep(1);

  close(G_plc_fd);

  return 0;
}

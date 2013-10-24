#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <plc_ldisc.h>

int plc_fd = -1;

void sighandler(int signum)
{
  if (signum != SIGINT)
    return;
  printf("User sent SIGINT. Exiting\n");
  close(plc_fd);
  exit(0);
}

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    fprintf(stderr, "Invalid usage - run with ""%s"" /dev/ttyS0 (where ttyS0 is the first COM port\n", argv[0]);
    return 1;
  }

  plc_fd = open(argv[1], O_RDWR | O_NOCTTY);
  if (plc_fd <= 0)
  {
    fprintf(stderr, "Could not open device %s (error %d). Exiting\n", argv[1], plc_fd);
    return 2;
  }
  struct termios term;
  tcgetattr (plc_fd, &term);
  term.c_lflag &= ~(ICANON | ECHO | ISIG);
  cfsetospeed (&term, B9600);
  cfsetispeed (&term, B9600);
  term.c_cflag |= PARENB ;
  term.c_cflag &= ~PARODD ;
  term.c_cflag &= ~CSIZE ;
  term.c_cflag |= (CS7 | CREAD | CLOCAL | CSTOPB);
  if (tcsetattr(plc_fd, TCSANOW, &term) < 0)
  {
    close(plc_fd);
    return 2;
  }

  signal(SIGINT, sighandler);

  long new_ldisc = N_PLC;
  int ret = ioctl(plc_fd, TIOCSETD, &new_ldisc);
  if (ret != 0)
  {
    fprintf(stderr, "Error setting line discipline for %s (%d)\n", argv[1], ret);
    return 2;
  }

  for (;;)
    sleep(1);

  close(plc_fd);

  return 0;
}

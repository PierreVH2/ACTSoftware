#include <stdio.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <plc_ldisc.h>

#define PLC_STAT_REQ       "@00RD0150001754*\r"
#define PLC_STAT_REQ_LEN   17
#define PLC_STAT_RESP_LEN  79
#define STAT_FCS_OFFS      75

extern const char *__progname;

char G_break_loop = 0;

void handle_sigint(int signum)
{
  if (signum != SIGINT)
  {
    fprintf(stderr, "[%s] Invalid signal number: %d\n", __progname, signum);
    return;
  }
  printf("[%s] Interrupted.\n", __progname);
  G_break_loop = 1;
}

static char hexchar2int(char c)
{
  if ((c>='0') && (c<='9'))
    return c - '0';
  if ((c>='A') && (c<='F'))
    return c - 'A' + 10;
  if ((c>='a') && (c<='f'))
    return c - 'a' + 10;
  return -1;
}

static int calc_fcs(const char *str, int length)
{
  int A=0, l;
  for (l=0; l<length ; l++)           // perform an exclusive or on each command string character in succession
    A = A ^ str[l];
  return A;
}

static int check_fcs(const char *str, int length)
{
  int fcs;
  fcs = hexchar2int(str[STAT_FCS_OFFS])*16 + hexchar2int(str[STAT_FCS_OFFS+1]);
  return (calc_fcs(str, STAT_FCS_OFFS) == fcs);
}

static int check_endcode(const char *str, int length)
{
  char code, tmp;
  if (length < 7)
    return 1;
  code = 0;
  tmp = hexchar2int(str[5]);
  if (tmp < 0)
    return 1;
  code += tmp*16;
  tmp = hexchar2int(str[6]);
  if (tmp < 0)
    return 1;
  code += tmp;
  return (tmp == 0);
}

int main(int argc, char **argv)
{
  if (argc <= 1)
  {
    fprintf(stderr, "[%s] No TTY device files specified.\n", __progname);
    return 1;
  }
  signal(SIGCHLD, SIG_IGN);
  int i;
  char ttydevname[100] = "";
  for (i=1; i<argc; i++)
  {
    int pid = fork();
    if (pid < 0)
    {
      fprintf(stderr, "[%s] Failed to fork process to check PLC TTY - %s\n", __progname, strerror(abs(pid)));
      return 1;
    }
    if (pid == 0)
    {
      snprintf(ttydevname, sizeof(ttydevname)-1, "%s", argv[i]);
      break;
    }
  }
  if (strlen(ttydevname) == 0)
  {
    printf("[%s] Nothing to check. Exiting.\n", __progname);
    return 0;
  }

  printf("[%s] Checking %s\n", __progname, ttydevname);
  int tty_fd = open(ttydevname, O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (tty_fd <= 0)
  {
    fprintf(stderr, "[%s %s] Could not open TTY %s - %s. Exiting.\n", __progname, ttydevname, ttydevname, strerror(errno));
    return 1;
  }
  struct termios term;
  int ret = tcgetattr (tty_fd, &term);
  if (ret < 0)
  {
    fprintf(stderr, "[%s %s] Could not get attributes of TTY %s - %s. Exiting.\n", __progname, ttydevname, ttydevname, strerror(errno));
    close(tty_fd);
    return 1;
  }
  term.c_lflag &= ~(ICANON | ECHO | ISIG);
  cfsetospeed (&term, B9600);
  cfsetispeed (&term, B9600);
  term.c_cflag |= PARENB ;
  term.c_cflag &= ~PARODD ;
  term.c_cflag &= ~CSIZE ;
  term.c_cflag |= (CS7 | CREAD | CLOCAL | CSTOPB);
  ret = tcsetattr(tty_fd, TCSANOW, &term);
  if (ret < 0)
  {
    fprintf(stderr, "[%s %s] Could not set attributes of TTY %s - %s. Exiting.\n", __progname, ttydevname, ttydevname, strerror(errno));
    close(tty_fd);
    return 1;
  }
  
  ret = 0;
  int pos = 0;
  char msg[PLC_STAT_RESP_LEN] = PLC_STAT_REQ;
  while (pos < PLC_STAT_REQ_LEN)
  {
    ret = write(tty_fd, &msg[pos], PLC_STAT_REQ_LEN-pos);
    if (ret < 0)
    {
      fprintf(stderr, "[%s %s] Could not write status request to TTY %s - %s. Exiting.\n", __progname, ttydevname, ttydevname, strerror(errno));
      close(tty_fd);
      return 1;
    }
    pos += ret;
  }
  printf("[%s %s] Status request sent. Awaiting response.\n", __progname, ttydevname);
  sleep(10);
  pos = 0;
  while (pos < PLC_STAT_RESP_LEN)
  {
    ret = read(tty_fd, &msg[pos], PLC_STAT_RESP_LEN-pos);
    if (ret < 0)
    {
      fprintf(stderr, "[%s %s] Could not read status response from TTY %s (%d bytes read so far) - %s. Exiting.\n", __progname, ttydevname, ttydevname, pos, strerror(errno));
      close(tty_fd);
      return 1;
    }
    pos += ret;
  }
  msg[pos] = '\0';
  printf("[%s %s] Response received (%d bytes) - %s\n", __progname, ttydevname, pos, msg);
//   printf("%s\n", msg);
  if (pos > PLC_STAT_RESP_LEN)
  {
    printf("Response string too long. Trimming to %d bytes.\n", PLC_STAT_RESP_LEN);
    pos = PLC_STAT_RESP_LEN;
  }

  if (strncmp(msg, PLC_STAT_REQ, 5) != 0)
  {
    fprintf(stderr, "[%s %s] Returned status has incorrect header.\n", __progname, ttydevname);
    close(tty_fd);
    return 1;
  }
  printf("[%s %s] Status response has correct header.\n", __progname, ttydevname);
  if ((!check_fcs(msg,pos+1)) || (!check_endcode(msg,pos)))
  {
    fprintf(stderr, "[%s %s] Returned status has invalid FCS/endcode.\n", __progname, ttydevname);
    close(tty_fd);
    return 1;
  }

  printf("[%s] Found TTY %s.\n", __progname, ttydevname);
  long new_ldisc = N_PLC;
  ret = ioctl(tty_fd, TIOCSETD, &new_ldisc);
  if (ret != 0)
  {
    fprintf(stderr, "[%s] Error setting line discipline for %s - %s. Exiting.\n", __progname, ttydevname, strerror(ret));
    close(tty_fd);
    return 1;
  }
  
  signal(SIGINT, handle_sigint);
  while (!G_break_loop)
    sleep(1);

  close(tty_fd);
  printf("[%s] Done. Exiting.\n", __progname);
  return 0;
}

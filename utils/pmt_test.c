#include <stdio.h>
#include <unistd.h>
#include <sys/io.h>
#include <time.h>
#include <signal.h>

#define COUNTER0   0X2A0
#define COUNTER1   0X2A2
#define COUNTER2   0X2A4
#define COUNTER3   0X2A6
#define CURCOUNTER COUNTER2
#define TTLIN      0X2AA
#define TTLOUT     0X2AC
#define PHOTIO     0X2AE
#define MODE0      0X9292

#define SAMPLE_P_MS   1

char G_break_loop = 0;

void handle_sigint(int signum)
{
  if (signum != SIGINT)
  {
    fprintf(stderr, "Invalid signal number: %d\n", signum);
    return;
  }
  printf("Interrupted by user.\n");
  G_break_loop = 1;
}

int main()
{
  printf("# WARNING: Do not run this programm while the PMT driver is running.\n");
  printf("# The times listed below are approximate, there may be a time drift.\n");
  printf("# This programme is not real-time, expect large errors.\n");
  printf("\n");
  signal(SIGINT, handle_sigint);

  ioperm (COUNTER0,0x10,1);
  outw (MODE0, PHOTIO);
  outw (0, TTLOUT);

  time_t tmp_time = time(NULL);
  double cur_time = tmp_time;
  unsigned short counts = inw(CURCOUNTER);
  unsigned long num_samples = 0;
  printf("# %f %hu\n", cur_time, counts);
  counts = 0;
  while (!G_break_loop)
  {
    usleep(SAMPLE_P_MS * 1000);
    counts += inw(CURCOUNTER);
//    inw(CURCOUNTER);
    num_samples++;
    cur_time += (SAMPLE_P_MS / 1000.0);
    if (num_samples*SAMPLE_P_MS < 1000)
     continue;
    printf("%ld %ld\n", (long)cur_time, (long)(counts*1000.0/SAMPLE_P_MS/num_samples));
    counts = 0;
    num_samples = 0;
  }
  return 0;
}


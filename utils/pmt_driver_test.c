#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <pmt_driver.h>

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

int main(int argc, char **argv)
{
  int pmt_fd = 0;
  FILE *outfile = NULL;
  if (argc > 1)
  {
    printf("Saving to %s\n", argv[1]);
    outfile = fopen(argv[1], "w");
  }
  else
    outfile = fopen("/dev/null", "w");
  if (outfile == NULL)
  {
    fprintf(stderr, "Error openining requested output file (%s)\n", argv[1]);
    return 1;
  }
  signal(SIGINT, handle_sigint);

  pmt_fd = open("/dev/" PMT_DEVICE_NAME, O_RDWR);
  if (pmt_fd <= 0)
  {
    fprintf(stderr, "Could not open PMT device driver character device /dev/%s - %s.\n", PMT_DEVICE_NAME, strerror(abs(pmt_fd)));
    fclose(outfile);
    return 1;
  }
  char pmt_stat;
  long ret = read(pmt_fd, &pmt_stat, 1);
  if (ret <= 0)
  {
    fprintf(stderr,"Error reading status from PMT driver - %s.\n", strerror(abs(ret)));
    fclose(outfile);
    close(pmt_fd);
    return 1;
  }

  struct pmt_information pmt_info;
  ret = ioctl(pmt_fd, IOCTL_GET_INFO, &pmt_info);
  if (ret < 0)
  {
    fprintf(stderr,"Error getting PMT information - %s.\n", strerror(abs(ret)));
    fclose(outfile);
    close(pmt_fd);
    return 1;
  }
  printf("# PMT ID: %s\n# Modes: %d\n# Time tag res (ns): %lu\n# Max/min sample rate (ns): %lu %lu\n# Dead time (ns): %lu\n#", pmt_info.pmt_id, pmt_info.modes, pmt_info.timetag_time_res_ns, pmt_info.max_sample_period_ns, pmt_info.min_sample_period_ns, pmt_info.dead_time_ns);
  struct pmt_command cmd = 
  {
    .mode = PMT_MODE_INTEG,
    .sample_length = 10,
    .prebin_num = 100,
    .repetitions = ~((unsigned long) 0)
  };
  ret = ioctl(pmt_fd, IOCTL_INTEG_CMD, &cmd);
  if (ret < 0)
  {
    fprintf(stderr, "Error sending integration command to PMT driver - %s\n", strerror(abs(ret)));
    fclose(outfile);
    close(pmt_fd);
    return 1;
  }

  struct pmt_integ_data data;
  char old_stat = 0;
  while (!G_break_loop)
  {
    sleep(1);
    ret = read(pmt_fd, &pmt_stat, 1);
    if (ret <= 0)
    {
      fprintf(stderr,"Error reading pmt_status from PMT driver - %s.\n", strerror(abs(ret)));
      continue;
    }
    if ((pmt_stat & PMT_STAT_UPDATE) == 0)
      continue;
    if (old_stat != pmt_stat)
    {
      old_stat = pmt_stat;
      if (pmt_stat & PMT_STAT_PROBE)
        printf("# Probing.\n");
      else if (pmt_stat & PMT_STAT_BUSY)
        printf("# Integrating.\n");
      else
      {
        printf("# Integration done/cancelled.\n");
        G_break_loop = 1;
      }
      if ((pmt_stat & PMT_STAT_DATA_READY) == 0)
        continue;
    }

    while (pmt_stat & PMT_STAT_DATA_READY)
    {
      ret = ioctl(pmt_fd, IOCTL_GET_INTEG_DATA, &data);
      if (ret < 0)
      {
        fprintf(stderr, "Failed to read integration data from PMT driver - %s.\n", strerror(abs(ret)));
        pmt_stat = 0;
        break;
      }
      if (data.error != 0)
      {
        if (data.error & PMT_ERR_ZERO)
          printf("# Zero counts.\n");
        if (data.error & PMT_ERR_WARN)
          printf("# High counts.\n");
        if (data.error & PMT_ERR_OVERILLUM)
          printf("# Overillumination!\n");
        if (data.error & PMT_ERR_OVERFLOW)
          printf("# Counter overflow.\n");
        if (data.error & PMT_ERR_TIME_SYNC)
          printf("# Time synchronisation loss.\n");
      }
      printf("%2lu:%02lu:%02lu.%03lu\t%10lu.%03lu\t%3hhd\t%10lu\t%10lu\n", data.start_time_s/3600, (data.start_time_s/60)%60, data.start_time_s%60, data.start_time_ns/1000000, data.start_time_s, data.start_time_ns/1000000, data.error, data.sample_period_ns/1000000, data.counts);
      fprintf(outfile, "%2lu:%02lu:%02lu.%03lu\t%10lu.%03lu\t%3hhd\t%10lu\t%10lu\n", data.start_time_s/3600, (data.start_time_s/60)%60, data.start_time_s%60, data.start_time_ns/1000000, data.start_time_s, data.start_time_ns/1000000, data.error, data.sample_period_ns/1000000, data.counts);
      long ret = read(pmt_fd, &pmt_stat, 1);
      if (ret < 0)
      {
        fprintf(stderr, "Error reading status from PMT driver.\n");
        pmt_stat = 0;
        break;
      }
    }
  }

  printf("# Exiting\n");
  ret = ioctl(pmt_fd, IOCTL_GET_CUR_INTEG, &data);
  if (ret < 0)
  {
    fprintf(stderr, "Failed to read integration data from PMT driver - %s.\n", strerror(abs(ret)));
    pmt_stat = 0;
    return 1;
  }
  if (data.error != 0)
  {
    if (data.error & PMT_ERR_ZERO)
      printf("# Zero counts.\n"); 
    if (data.error & PMT_ERR_WARN)
      printf("# High counts.\n");
    if (data.error & PMT_ERR_OVERILLUM)
      printf("# Overillumination!\n");
    if (data.error & PMT_ERR_OVERFLOW)
      printf("# Counter overflow.\n"); 
    if (data.error & PMT_ERR_TIME_SYNC)
      printf("# Time synchronisation loss.\n");
  }
  printf("# Final data: %2lu:%02lu:%02lu.%03lu\t%10lu.%03lu\t%3hhd\t%10lu\t%10lu\n", data.start_time_s/3600, (data.start_time_s/60)%60, data.start_time_s%60, data.start_time_ns/1000000, data.start_time_s, data.start_time_ns/1000000, data.error, data.sample_period_ns/1000000, data.counts);
  close(pmt_fd);
  fclose(outfile);
  return 0;
}

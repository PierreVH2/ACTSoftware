#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <motor_driver.h>

int main()
{
  int motor_fd = open("/dev/" MOTOR_DEVICE_NAME, O_RDWR | O_NONBLOCK);
  if (motor_fd < 0)
  {
    fprintf(stderr, "Failed to open motor device (/dev/%s) - %s\n", MOTOR_DEVICE_NAME, strerror(errno));
    return 1;
  }
  char motor_stat;
  int ret = read(motor_fd, &motor_stat, 1);
  if (ret < 0)
  {
    fprintf(stderr, "Failed to read motor status - %s\n", strerror(errno));
    close(motor_fd);
    return 1;
  }
  printf("Current motor status:\n%s%s%s%s%s%s%s\n\n",
         (motor_stat & MOTOR_STAT_HA_INIT) > 0 ? "HA INIT\t" : "",
         (motor_stat & MOTOR_STAT_DEC_INIT) ? "DEC INIT\t" : "",
         (motor_stat & MOTOR_STAT_TRACKING) ? "TRACKING\t" : "",
         (motor_stat & MOTOR_STAT_GOTO) ? "GOTO\t" : "",
         (motor_stat & MOTOR_STAT_CARD) ? "CARD. MOVE\t" : "",
         (motor_stat & MOTOR_STAT_ERR_LIMS) ? "LIMIT\t" : "",
         (motor_stat & MOTOR_STAT_ALLSTOP) ? "EMGNY STOP\t" : "");
  
  char limit_stat;
  ret = ioctl(motor_fd, IOCTL_MOTOR_GET_LIMITS, &limit_stat);
  if (ret < 0)
    fprintf(stderr, "Failed to read motor limits - %s\n", strerror(errno));
  else
    printf("Limit status:\n%s%s%s%s\n\n",
           MOTOR_LIM_N(limit_stat) ? "NORTH\t" : "",
           MOTOR_LIM_S(limit_stat) ? "SOUTH\t" : "",
           MOTOR_LIM_E(limit_stat) ? "EAST\t" : "",
           MOTOR_LIM_W(limit_stat) ? "WEST\t" : "");
  
  struct motor_tel_coord coord;
  ret = ioctl(motor_fd, IOCTL_MOTOR_GET_MOTOR_POS, &coord);
  if (ret < 0)
    fprintf(stderr, "Failed to read motor coordinates - %s\n", strerror(errno));
  else
    printf("Motor coordinates (HA steps, Dec steps):  %d %d\n\n", coord.tel_ha, coord.tel_dec);
  
  close(motor_fd);
  return 0;
}

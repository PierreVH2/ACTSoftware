#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <string.h>
#include <errno.h>
#include <motor_driver.h>

int main(int argc, char ** argv)
{
  if (argc != 3)
  {
    fprintf(stderr, "Invalid usage.\n\nUse as follows:\n\t%s HA DEC\n\nHA must be specified as the (integer) number of motor steps from the WESTERN motor limit.\nDEC must be specifed as the (integer) number of motor steps from the SOUTHERN motor limit.\n", argv[0]);
    return 2;
  }
  int ha_steps, dec_steps;
  char * end;
  ha_steps = strtol(argv[1], &end, 10);
  if (end != &argv[1][strlen(argv[1])])
  {
    fprintf(stderr, "Invalid value specifed for HA: %s\n", argv[1]);
    return 2;
  }
  dec_steps = strtol(argv[2], &end, 10);
  if (end != &argv[2][strlen(argv[2])])
  {
    fprintf(stderr, "Invalid value specifed for DEC: %s\n", argv[2]);
    return 2;
  }
  printf("WARNING: This programme allows the user to set the current coordinates of the telescope, as recorded in the motor driver and is provided purely for remote debugging purposes. Setting the coordinates to anything except the current and real telescope coordinates may result in damage to the telescope. DO NOT use this programme unless you know the telescope coordinates.\n\nEntered coordinates:\nHA steps: %d\nDEC steps: %d\n\nDo you wish to continue? [y/n]  \n", ha_steps, dec_steps);
  char ans = getchar();
  printf("\n\n");
  if ((ans != 'Y') && (ans != 'y'))
  {
    printf("Aborted.\n");
    return 0;
  }
  
  int motor_fd = open("/dev/" MOTOR_DEVICE_NAME, O_RDWR | O_NONBLOCK);
  if (motor_fd < 0)
  {
    fprintf(stderr, "Failed to open motor device (/dev/%s) - %s\n\nAborted.\n", MOTOR_DEVICE_NAME, strerror(errno));
    return 1;
  }
  int ret = ioctl(motor_fd, IOCTL_MOTOR_SET_HA_STEPS, &ha_steps);
  if (ret < 0)
  {
    fprintf(stderr, "Failed to set motor driver HA steps - %s.\nWARNING: Motor coordinates may be partially set. Proceed with extreme caution - preferably initialise the telescope with the hand paddle in the dome.\n", strerror(errno));
    close(motor_fd);
    return 1;
  }
  ret = ioctl(motor_fd, IOCTL_MOTOR_SET_DEC_STEPS, &dec_steps);
  if (ret < 0)
  {
    fprintf(stderr, "Failed to set motor driver DEC steps - %s.\nWARNING: Motor coordinates may be partially set. Proceed with extreme caution - preferably initialise the telescope with the hand paddle in the dome.\n", strerror(errno));
    close(motor_fd);
    return 1;
  }
  printf("Success.\n");
  close(motor_fd);
  return 0;
}

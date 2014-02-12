#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <merlin_driver.h>

int main()
{
  int ret;
  int ccd_fd = open("/dev/" MERLIN_DEVICE_NAME, O_RDWR);
  if (ccd_fd < 0)
  {
    fprintf(stderr, "Error opening CCD character device.\n");
    return 1;
  }
  struct ccd_modes modes;
  ret = ioctl(ccd_fd, IOCTL_GET_MODES, &modes);
  if (ret < 0)
  {
    fprintf(stderr, "Error getting camera modes from CCD driver (%d) - %s.\n", ret, strerror(abs(ret)));
    close(ccd_fd);
    return 1;
  }
  printf("CCD ID: %s\n", modes.ccd_id);
  struct ccd_cmd exp_cmd;
  exp_cmd.prebin_x = exp_cmd.prebin_y = 1;
  exp_cmd.win_start_x = exp_cmd.win_start_y = 0;
  exp_cmd.win_width = modes.max_width_px;
  exp_cmd.win_height = modes.max_height_px;
  ccd_cmd_exp_t(0.04, exp_cmd);
  printf("Sending CCD exposure command\n");
  ret = ioctl(ccd_fd, IOCTL_ORDER_EXP, &exp_cmd);
  if (ret < 0)
  {
    fprintf(stderr, "Error ordering exposure from CCD driver (%d) - %s.\n", ret, strerror(abs(ret)));
    close(ccd_fd);
    return 1;
  }
  char cur_stat = 0;
  printf("Waiting for image\n");
  while ((cur_stat & CCD_IMG_READY) == 0)
  {
    ret = read(ccd_fd, &cur_stat, sizeof(char));
    if (cur_stat < 0)
    {
      fprintf(stderr, "Error reading from CCD character device - %s\n", strerror(ret));
      close(ccd_fd);
      return 1;
    }
    if (cur_stat & CCD_ERROR)
    {
      fprintf(stderr, "Driver reported internal CCD error.\n");
      close(ccd_fd);
      return 1;
    }
    sleep(1);
  }
  printf("Image received.\n");
  struct merlin_img cur_img;
  ret = ioctl(ccd_fd, IOCTL_GET_IMAGE, &cur_img);
  if (ret < 0)
  {
    fprintf(stderr, "Error retrieving image from CCD driver - %s\n", strerror(ret));
    close(ccd_fd);
    return 1;
  }
  printf("Exposure started at %02d:%02d:%05.3f\n", (int)trunc(cur_img.img_params.start_sec/3600.0), (int)fmod(trunc(cur_img.img_params.start_sec/60.0),60.0), fmod(cur_img.img_params.start_sec,60.0) + cur_img.img_params.start_nanosec/1000000000.0);
  printf("Exposure lasted %f s\n", ccd_img_exp_t(cur_img.img_params));
  unsigned short img_width=cur_img.img_params.win_width/cur_img.img_params.prebin_x, img_height=cur_img.img_params.win_height/cur_img.img_params.prebin_y;
  printf("Image dimensiones: %hux%hu (total %lu pixels)\n", img_width, img_height, cur_img.img_params.img_len);
  printf("Write image data to file? (y/n) ");
  char ans = getc(stdin);
  if ((ans == 'y') || (ans == 'Y'))
  {
    FILE *fout = fopen("imgdata.txt", "w");
    unsigned int i;
    for (i=0; i<cur_img.img_params.img_len; i++)
    {
      fprintf(fout, "%hhu ", cur_img.img_data[i%MERLIN_MAX_IMG_LEN]);
      if (i%img_width == 0)
        printf("\n");
    }
    printf("Done.\n");
  }
  printf("Write image data to stdout? (y/n) ");
  ans = getc(stdin);
  if ((ans == 'y') || (ans == 'Y'))
  {
    unsigned int i;
    for (i=0; i<cur_img.img_params.img_len; i++)
    {
      printf("%hhu ", cur_img.img_data[i%MERLIN_MAX_IMG_LEN]);
      if (i%img_width == 0)
        printf("\n\n");
    }
    printf("Done.\n");
  }
  close(ccd_fd);
  return 0;
}

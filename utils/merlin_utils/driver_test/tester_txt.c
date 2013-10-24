#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <merlin_driver/merlin_driver.h>

int main()
{
  int ret;
  int ccd_fd = open(MERLIN_DEVICE_FILENAME, O_RDWR);
  if (ccd_fd < 0)
  {
    fprintf(stderr, "Error opening CCD character device.\n");
    return 1;
  }
  struct ccd_cmd exp_cmd;
  exp_cmd.start_at_sec = 0;
  exp_cmd.frame_transfer = CCD_MODE_FRAME_XFER_ON;
  exp_cmd.prebin = CCD_MODE_PREBIN_1;
  exp_cmd.window = 0;
  exp_cmd.exp_t_msec = 40;
  printf("Sending CCD exposure\n");
  ret = ioctl(ccd_fd, IOCTL_ORDER_EXP, &exp_cmd);
  if (ret < 0)
  {
    fprintf(stderr, "Error requesting image from CCD driver (%d) - %s.\n", ret, strerror(abs(ret)));
    close(ccd_fd);
    return 1;
  }
  char cur_stat = 0;
  printf("Waiting for image\n");
  while ((cur_stat & IMG_READY) == 0)
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
  printf("Exposure started at %hhd:%hhu:%hhu.%hu\n", cur_img.img_params.start_hrs, cur_img.img_params.start_min, cur_img.img_params.start_sec, cur_img.img_params.start_msec);
  printf("Exposure lasted %u ms\n", cur_img.img_params.exp_t_msec);
  printf("Image dimensiones: %hux%hu (total %lu pixels)\n", cur_img.img_params.img_width, cur_img.img_params.img_height, cur_img.img_params.img_len);
  printf("Write image data to file? (y/n) ");
  char ans = getc(stdin);
  if ((ans == 'y') || (ans == 'Y'))
  {
    FILE *fout = fopen("imgdata.txt", "w");
    unsigned int i;
    for (i=0; i<cur_img.img_params.img_len; i++)
    {
      fprintf(fout, "%hhu ", cur_img.img_data[i%MERLIN_MAX_IMG_LEN]);
      if (i%cur_img.img_params.img_width == 0)
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
      if (i%cur_img.img_params.img_width == 0)
	printf("\n\n");
    }
    printf("Done.\n");
  }
  close(ccd_fd);
  return 0;
}
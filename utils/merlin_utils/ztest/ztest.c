

#include <stdio.h>
#include <unistd.h>
#include <sys/io.h>

#define   READ_DATA        0x2b8
#define   WRITE_DATA       0x2b9
#define   INPUT_STATUS     0x2ba
#define   OUTPUT_STATUS    0x2bb
#define   PORT_8212        0x2bc

void init_C012()
{
  outb(0x08,PORT_8212);
  outb(0x00,PORT_8212);
}

int main()
{
  ioperm (READ_DATA,6,1);

  char status, i;
  init_C012();

  for(;;)
  {
    if(inb(OUTPUT_STATUS)==0)
      init_C012();
    outb('Z',WRITE_DATA);
    status = -1;

    for(i=0; i<100; i++)
    {
      if(inb(INPUT_STATUS)==1)
      {
        status=inb(READ_DATA);
        printf("%c\n", status);
      }
    }
    if (status == -1)
      printf("No response received.\n");
    sleep(1);
  }
  return 0;
}


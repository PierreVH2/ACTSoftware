Utility tester_txt

This utility was developed to test basic communications with the MERLIN CCD driver. If this test utility
does not report an error, all higher-level software should work as well.

To compile this utility, run:
  gcc -Wall -Wextra -I<merlin driver source dir> tester_txt.c -o tester_txt
Where <merlin driver source dir> is the global or relative path where the merlin_driver source directory 
(which should contain the merlin_driver.h file) may be found.

To run, simply execute the binary produced by the compiler.

If successful, the tester utility will output the image as a series of integers representing the value of
each pixel.
#!/bin/bash

export ACT_DEVEL=/home/cassiopea/tmp/act_control-20110528/
export ACT_DRIVERS=/home/cassiopea/tmp/act_drivers-20110615/
gcc -Wall -Wextra -pthread -I/usr/include/gtk-2.0 -I/usr/lib/gtk-2.0/include -I/usr/include/atk-1.0 -I/usr/include/cairo -I/usr/include/gdk-pixbuf-2.0 -I/usr/include/pango-1.0 -I/usr/include/glib-2.0 -I/usr/lib/glib-2.0/include -I/usr/include/pixman-1 -I/usr/include/freetype2 -I/usr/include/libpng14  -pthread -lgtk-x11-2.0 -lgdk-x11-2.0 -latk-1.0 -lgio-2.0 -lpangoft2-1.0 -lpangocairo-1.0 -lgdk_pixbuf-2.0 -lm -lcairo -lpng14 -lpango-1.0 -lfreetype -lfontconfig -lgobject-2.0 -lgmodule-2.0 -lgthread-2.0 -lrt -lglib-2.0   -I/home/cassiopea/tmp/act_control-20110528/ -I/home/cassiopea/tmp/act_drivers-20110615/ -lcfitsio -largtable2 ./acqsim.c -o ./acqsim

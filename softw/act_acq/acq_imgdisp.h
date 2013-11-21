#ifndef ACQ_IMGDISP_H
#define ACQ_IMGDISP_H

#include <gtk/gtk.h>
#include <act_timecoord.h>

int create_imgdisp_objs(const char *actfiles_path, MYSQL *conn, GtkWidget *evb_imgdisp);
void update_imgdisp(struct merlin_img *newimg, struct rastruct *ra, struct decstruct *dec);

#endif

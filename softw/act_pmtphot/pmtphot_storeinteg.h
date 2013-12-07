#ifndef PMTPHOT_STOREINTEG
#define PMTPHOT_STOREINTEG

#include <gtk/gtk.h>
#include <mysql/mysql.h>
#include "pmtfuncs.h"

struct storeinteg_objects
{
  MYSQL *mysql_conn;
  FILE *bak_phot_fd;
  GtkWidget *evb_store_stat, *lbl_store_stat;
};

struct storeinteg_objects *create_storeinteg(GtkWidget *container, MYSQL *conn);
void finalise_storeinteg(struct storeinteg_objects *objs);
void storeinteg(struct storeinteg_objects *objs, struct pmtintegstruct *pmtinteg, int num_buffered);

#endif